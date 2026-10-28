// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The bridge between the Android app (com.redmarmoset.marconi.NativeSession) and MarconiCore: one native session per
// NativeSession object. Calls from Kotlin arrive on the app's main thread; the session's callbacks arrive on the
// connection thread and are handed straight to the Kotlin object, which moves them to the main thread itself.

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/QuickConnect.h>
#include <MarconiCore/RemoteSession.h>

#include <Log/Log.h>

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    JavaVM* g_vm = nullptr;

    // The JNIEnv of the calling thread. The connection thread is attached the first time it asks and detached
    // when it ends; the main thread is already attached and left alone.
    struct ThreadEnv
    {
        JNIEnv* env = nullptr;
        bool attached = false;

        ~ThreadEnv()
        {
            if (attached && g_vm != nullptr)
            {
                g_vm->DetachCurrentThread();
            }
        }
    };

    thread_local ThreadEnv t_env;

    JNIEnv* Env()
    {
        if (t_env.env == nullptr && g_vm != nullptr)
        {
            if (g_vm->GetEnv(reinterpret_cast<void**>(&t_env.env), JNI_VERSION_1_6) == JNI_EDETACHED)
            {
                if (g_vm->AttachCurrentThread(&t_env.env, nullptr) == JNI_OK)
                {
                    t_env.attached = true;
                }
                else
                {
                    t_env.env = nullptr;
                }
            }
        }
        return t_env.env;
    }

    std::string FromJava(JNIEnv* env, jstring text)
    {
        if (text == nullptr)
        {
            return "";
        }
        const char* chars = env->GetStringUTFChars(text, nullptr);
        std::string result = chars != nullptr ? chars : "";
        if (chars != nullptr)
        {
            env->ReleaseStringUTFChars(text, chars);
        }
        return result;
    }

    // A Java String from UTF-8 bytes, through String(byte[], "UTF-8") so that characters outside the Basic
    // Multilingual Plane (which NewStringUTF can't take) arrive intact.
    jstring ToJava(JNIEnv* env, const std::string& text)
    {
        jclass stringClass = env->FindClass("java/lang/String");
        jmethodID constructor = env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(text.size()));
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()),
                                reinterpret_cast<const jbyte*>(text.data()));
        jstring encoding = env->NewStringUTF("UTF-8");
        jstring result = static_cast<jstring>(env->NewObject(stringClass, constructor, bytes, encoding));
        env->DeleteLocalRef(encoding);
        env->DeleteLocalRef(bytes);
        env->DeleteLocalRef(stringClass);
        return result;
    }

    std::string Latin1ToUtf8(const std::string& latin1)
    {
        std::string utf8;
        for (unsigned char c : latin1)
        {
            if (c < 0x80)
            {
                utf8 += static_cast<char>(c);
            }
            else
            {
                utf8 += static_cast<char>(0xc0 | (c >> 6));
                utf8 += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        return utf8;
    }

    std::string HexOf(const std::vector<uint8_t>& bytes)
    {
        static const char* digits = "0123456789abcdef";
        std::string hex;
        for (uint8_t b : bytes)
        {
            hex += digits[b >> 4];
            hex += digits[b & 15];
        }
        return hex;
    }

    std::vector<uint8_t> BytesOf(const std::string& hex)
    {
        std::vector<uint8_t> bytes;
        auto value = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            int high = value(hex[i]);
            int low = value(hex[i + 1]);
            if (high < 0 || low < 0)
            {
                return {};
            }
            bytes.push_back(static_cast<uint8_t>(high * 16 + low));
        }
        return bytes;
    }

    // A pending Java exception would make the next JNI call abort the process: report it and carry on.
    void ClearException(JNIEnv* env)
    {
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }

    void LogToLogcat(Log::Level level, const char* tag, const char* message)
    {
        int priority = ANDROID_LOG_DEBUG;
        switch (level)
        {
            case Log::Level::Info: priority = ANDROID_LOG_INFO; break;
            case Log::Level::Warning: priority = ANDROID_LOG_WARN; break;
            case Log::Level::Error: priority = ANDROID_LOG_ERROR; break;
            case Log::Level::Verbose: priority = ANDROID_LOG_DEBUG; break;
        }
        __android_log_print(priority, tag, "%s", message);
    }

    // One connection, owned by a Kotlin NativeSession. The Kotlin object's callback methods are looked up once.
    class Session : public Marconi::SessionListener
    {
    public:
        Session(JNIEnv* env, jobject owner) : m_owner(env->NewGlobalRef(owner))
        {
            jclass cls = env->GetObjectClass(owner);
            m_onConnected = env->GetMethodID(cls, "onConnected", "(IILjava/lang/String;)V");
            m_onFramebufferUpdated = env->GetMethodID(cls, "onFramebufferUpdated", "(IIII)V");
            m_onDesktopResized = env->GetMethodID(cls, "onDesktopResized", "(II)V");
            m_onServerText = env->GetMethodID(cls, "onServerText", "(Ljava/lang/String;)V");
            m_onBell = env->GetMethodID(cls, "onBell", "()V");
            m_onDisconnected = env->GetMethodID(cls, "onDisconnected", "(Ljava/lang/String;ZZ)V");
            m_askCredentials = env->GetMethodID(cls, "askCredentials", "(ILjava/lang/String;)[Ljava/lang/String;");
            m_askTrustCertificate = env->GetMethodID(
                cls, "askTrustCertificate",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
            m_askTrustSshHostKey = env->GetMethodID(
                cls, "askTrustSshHostKey", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
            env->DeleteLocalRef(cls);
        }

        ~Session() override
        {
            Disconnect();
            m_session.reset();
            if (JNIEnv* env = Env())
            {
                env->DeleteGlobalRef(m_owner);
            }
        }

        // Starts connecting. Returns a message when the address can't be used at all; otherwise the outcome
        // arrives through onConnected or onDisconnected.
        std::string Connect(const std::string& addressText, const std::string& username,
                            const std::string& password, const std::string& trustedCertificateHex)
        {
            Disconnect();
            Marconi::QuickConnectOptions options;
            options.address = addressText;
            options.username = username;
            options.password = password;
            options.trustedCertificateSha256 = BytesOf(trustedCertificateHex);
            options.clientName = "Marconi for Android";
            std::string error;
            std::unique_ptr<Marconi::RemoteSession> session = Marconi::QuickConnect(options, *this, error);
            if (session)
            {
                m_session = std::move(session);
            }
            return error;
        }

        void Disconnect()
        {
            if (m_session)
            {
                m_session->Disconnect();
            }
        }

        Marconi::RemoteSession* Current() { return m_session.get(); }

        // Copies a rectangle of the picture into an RGBA_8888 bitmap of the same size as the picture, turning
        // 0x00RRGGBB pixels into the R,G,B,A bytes Android wants. False when the bitmap doesn't match the picture.
        bool CopyFramebuffer(JNIEnv* env, jobject bitmap, int x, int y, int width, int height)
        {
            if (!m_session)
            {
                return false;
            }
            AndroidBitmapInfo info;
            if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
                info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
            {
                return false;
            }
            void* pixels = nullptr;
            if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
            {
                return false;
            }
            bool ok = false;
            {
                Marconi::Rfb::Framebuffer& framebuffer = m_session->GetFramebuffer();
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                if (static_cast<int>(info.width) == framebuffer.GetWidth() &&
                    static_cast<int>(info.height) == framebuffer.GetHeight())
                {
                    int left = std::max(0, x);
                    int top = std::max(0, y);
                    int right = std::min(framebuffer.GetWidth(), x + width);
                    int bottom = std::min(framebuffer.GetHeight(), y + height);
                    for (int row = top; row < bottom; ++row)
                    {
                        const uint32_t* source = framebuffer.Row(row) + left;
                        uint32_t* destination = reinterpret_cast<uint32_t*>(
                            static_cast<uint8_t*>(pixels) + static_cast<size_t>(row) * info.stride) + left;
                        for (int column = left; column < right; ++column)
                        {
                            uint32_t pixel = *source++;
                            *destination++ = 0xff000000u | ((pixel & 0xffu) << 16) | (pixel & 0xff00u) |
                                             ((pixel >> 16) & 0xffu);
                        }
                    }
                    ok = true;
                }
            }
            AndroidBitmap_unlockPixels(env, bitmap);
            return ok;
        }

        int FramebufferWidth()
        {
            if (!m_session) return 0;
            Marconi::Rfb::Framebuffer& framebuffer = m_session->GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            return framebuffer.GetWidth();
        }

        int FramebufferHeight()
        {
            if (!m_session) return 0;
            Marconi::Rfb::Framebuffer& framebuffer = m_session->GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            return framebuffer.GetHeight();
        }

        // SessionListener, on the connection thread.
        void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return;
            jstring name = ToJava(env, serverInit.name);
            env->CallVoidMethod(m_owner, m_onConnected, serverInit.width, serverInit.height, name);
            env->DeleteLocalRef(name);
            ClearException(env);
        }

        void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override
        {
            if (rects.empty()) return;
            Marconi::Rfb::Rect all = rects.front();
            for (size_t i = 1; i < rects.size(); ++i)
            {
                all = all.Union(rects[i]);
            }
            JNIEnv* env = Env();
            if (env == nullptr) return;
            env->CallVoidMethod(m_owner, m_onFramebufferUpdated, all.x, all.y, all.width, all.height);
            ClearException(env);
        }

        void OnDesktopResized(int width, int height) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return;
            env->CallVoidMethod(m_owner, m_onDesktopResized, width, height);
            ClearException(env);
        }

        void OnServerCutText(const std::string& latin1Text) override
        {
            SendServerText(Latin1ToUtf8(latin1Text));
        }

        void OnServerClipboard(const Marconi::Rfb::ClipboardContent& content) override
        {
            if (!content.utf8Text.empty())
            {
                SendServerText(content.utf8Text);
            }
        }

        void OnBell() override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return;
            env->CallVoidMethod(m_owner, m_onBell);
            ClearException(env);
        }

        void OnDisconnected(const Marconi::DisconnectInfo& info) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return;
            jstring error = ToJava(env, info.error);
            env->CallVoidMethod(m_owner, m_onDisconnected, error, info.authenticationFailed ? JNI_TRUE : JNI_FALSE,
                                info.wasConnected ? JNI_TRUE : JNI_FALSE);
            env->DeleteLocalRef(error);
            ClearException(env);
        }

        bool OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return false;
            jstring username = ToJava(env, credentials.username);
            jobjectArray answer = static_cast<jobjectArray>(
                env->CallObjectMethod(m_owner, m_askCredentials, static_cast<jint>(kind), username));
            env->DeleteLocalRef(username);
            ClearException(env);
            if (answer == nullptr || env->GetArrayLength(answer) < 2)
            {
                return false;
            }
            jstring user = static_cast<jstring>(env->GetObjectArrayElement(answer, 0));
            jstring password = static_cast<jstring>(env->GetObjectArrayElement(answer, 1));
            credentials.username = FromJava(env, user);
            credentials.password = FromJava(env, password);
            env->DeleteLocalRef(user);
            env->DeleteLocalRef(password);
            env->DeleteLocalRef(answer);
            return true;
        }

        bool OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return false;
            jstring subject = ToJava(env, certificate.subject);
            jstring issuer = ToJava(env, certificate.issuer);
            jstring validUntil = ToJava(env, certificate.validUntil);
            jstring problem = ToJava(env, certificate.problem);
            jstring fingerprint = ToJava(env, HexOf(certificate.sha256));
            jboolean trusted = env->CallBooleanMethod(m_owner, m_askTrustCertificate, subject, issuer, validUntil,
                                                      problem, fingerprint);
            for (jstring s : {subject, issuer, validUntil, problem, fingerprint})
            {
                env->DeleteLocalRef(s);
            }
            ClearException(env);
            return trusted == JNI_TRUE;
        }

        bool OnUntrustedSshHostKey(const Marconi::Net::SshHostKeyQuestion& question) override
        {
            JNIEnv* env = Env();
            if (env == nullptr) return false;
            jstring host = ToJava(env, question.host);
            jstring algorithm = ToJava(env, question.algorithm);
            jstring fingerprint = ToJava(env, question.fingerprint);
            jstring previous = ToJava(env, question.previous);
            jboolean trusted = env->CallBooleanMethod(m_owner, m_askTrustSshHostKey, host, algorithm, fingerprint,
                                                      previous);
            for (jstring s : {host, algorithm, fingerprint, previous})
            {
                env->DeleteLocalRef(s);
            }
            ClearException(env);
            return trusted == JNI_TRUE;
        }

    private:
        void SendServerText(const std::string& utf8)
        {
            JNIEnv* env = Env();
            if (env == nullptr) return;
            jstring text = ToJava(env, utf8);
            env->CallVoidMethod(m_owner, m_onServerText, text);
            env->DeleteLocalRef(text);
            ClearException(env);
        }

        jobject m_owner;
        jmethodID m_onConnected;
        jmethodID m_onFramebufferUpdated;
        jmethodID m_onDesktopResized;
        jmethodID m_onServerText;
        jmethodID m_onBell;
        jmethodID m_onDisconnected;
        jmethodID m_askCredentials;
        jmethodID m_askTrustCertificate;
        jmethodID m_askTrustSshHostKey;
        std::unique_ptr<Marconi::RemoteSession> m_session;
    };

    Session* SessionOf(jlong handle)
    {
        return reinterpret_cast<Session*>(static_cast<intptr_t>(handle));
    }
} // namespace

extern "C"
{
    JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
    {
        g_vm = vm;
        Log::SetCallback(LogToLogcat);
        return JNI_VERSION_1_6;
    }

    JNIEXPORT jlong JNICALL Java_com_redmarmoset_marconi_NativeSession_create(JNIEnv* env, jobject self)
    {
        return static_cast<jlong>(reinterpret_cast<intptr_t>(new Session(env, self)));
    }

    JNIEXPORT void JNICALL Java_com_redmarmoset_marconi_NativeSession_destroy(JNIEnv*, jobject, jlong handle)
    {
        delete SessionOf(handle);
    }

    JNIEXPORT jstring JNICALL Java_com_redmarmoset_marconi_NativeSession_connect(
        JNIEnv* env, jobject, jlong handle, jstring address, jstring username, jstring password,
        jstring trustedCertificateHex)
    {
        std::string error = SessionOf(handle)->Connect(FromJava(env, address), FromJava(env, username),
                                                       FromJava(env, password), FromJava(env, trustedCertificateHex));
        return error.empty() ? nullptr : ToJava(env, error);
    }

    JNIEXPORT void JNICALL Java_com_redmarmoset_marconi_NativeSession_disconnect(JNIEnv*, jobject, jlong handle)
    {
        SessionOf(handle)->Disconnect();
    }

    JNIEXPORT jboolean JNICALL Java_com_redmarmoset_marconi_NativeSession_copyFramebuffer(
        JNIEnv* env, jobject, jlong handle, jobject bitmap, jint x, jint y, jint width, jint height)
    {
        return SessionOf(handle)->CopyFramebuffer(env, bitmap, x, y, width, height) ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jint JNICALL Java_com_redmarmoset_marconi_NativeSession_framebufferWidth(JNIEnv*, jobject, jlong handle)
    {
        return SessionOf(handle)->FramebufferWidth();
    }

    JNIEXPORT jint JNICALL Java_com_redmarmoset_marconi_NativeSession_framebufferHeight(JNIEnv*, jobject, jlong handle)
    {
        return SessionOf(handle)->FramebufferHeight();
    }

    JNIEXPORT void JNICALL Java_com_redmarmoset_marconi_NativeSession_sendPointer(
        JNIEnv*, jobject, jlong handle, jint x, jint y, jint buttons)
    {
        if (Marconi::RemoteSession* session = SessionOf(handle)->Current())
        {
            session->SendPointerEvent(x, y, static_cast<uint8_t>(buttons));
        }
    }

    JNIEXPORT void JNICALL Java_com_redmarmoset_marconi_NativeSession_sendKey(
        JNIEnv*, jobject, jlong handle, jint keysym, jboolean down)
    {
        if (Marconi::RemoteSession* session = SessionOf(handle)->Current())
        {
            session->SendKeyEvent(static_cast<uint32_t>(keysym), down == JNI_TRUE);
        }
    }

    JNIEXPORT jint JNICALL Java_com_redmarmoset_marconi_NativeSession_keysymForCodepoint(JNIEnv*, jclass, jint codepoint)
    {
        return static_cast<jint>(Marconi::Keysym::FromUnicode(static_cast<uint32_t>(codepoint)));
    }

    JNIEXPORT void JNICALL Java_com_redmarmoset_marconi_NativeSession_requestFullUpdate(JNIEnv*, jobject, jlong handle)
    {
        if (Marconi::RemoteSession* session = SessionOf(handle)->Current())
        {
            session->RequestFullUpdate();
        }
    }

    JNIEXPORT jboolean JNICALL Java_com_redmarmoset_marconi_NativeSession_isEncrypted(JNIEnv*, jobject, jlong handle)
    {
        Marconi::RemoteSession* session = SessionOf(handle)->Current();
        return session != nullptr && session->IsEncrypted() ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jstring JNICALL Java_com_redmarmoset_marconi_NativeSession_encryptionName(JNIEnv* env, jobject, jlong handle)
    {
        Marconi::RemoteSession* session = SessionOf(handle)->Current();
        return ToJava(env, session != nullptr ? session->GetEncryptionName() : std::string());
    }
}
