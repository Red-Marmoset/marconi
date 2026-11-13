import java.io.ByteArrayOutputStream

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// The Marconi checkout: Apps/Android/app is three levels down.
val marconiRoot: File = rootProject.projectDir.parentFile.parentFile
val marconiAbis: List<String> = (project.findProperty("marconiAbis") as String? ?: "arm64-v8a").split(",").map { it.trim() }
val marconiConfig: String = project.findProperty("marconiConfig") as String? ?: "Release"

// MAJOR.MINOR.PATCH from Project.json plus the commit count, as the desktop builds number themselves.
val marconiVersion: String = Regex("\"version\"\\s*:\\s*\"([^\"]+)\"")
    .find(File(marconiRoot, "Project.json").readText())?.groupValues?.get(1) ?: "0.0.0"
val marconiRevision: Int = try {
    val output = ByteArrayOutputStream()
    project.exec {
        commandLine("git", "rev-list", "--count", "HEAD")
        workingDir = marconiRoot
        standardOutput = output
    }
    output.toString().trim().toInt()
} catch (e: Exception) {
    1
}

val isWindows = System.getProperty("os.name").lowercase().contains("windows")

// Which environment variables hold the release key is Project.json's to say (its "signing" section); Una's
// BuildTool reads it and prints a variable's name, or a value Project.json fixes. Null when BuildTool isn't built
// or is too old to answer, and the release build is then signed with the debug key.
val unaBuildTool: File? = run {
    val engine = File(System.getenv("UNA_ENGINE_PATH") ?: if (isWindows) "C:\\code\\engine" else File(marconiRoot, "../engine").path)
    listOf("Release", "MinSizeRel", "Debug", "Bootstrap")
        .map { File(engine, "Build/BuildTool/$it/" + if (isWindows) "BuildTool.exe" else "BuildTool") }
        .firstOrNull { it.isFile }
}
fun signingSetting(switch: String, setting: String): String? {
    val tool = unaBuildTool ?: return null
    return try {
        val output = ByteArrayOutputStream()
        project.exec {
            commandLine(tool.path, File(marconiRoot, "Project.json").path, switch, setting)
            standardOutput = output
        }
        output.toString().trim().ifEmpty { null }
    } catch (e: Exception) {
        logger.warn("Marconi: ${tool.path} couldn't answer $switch $setting; update the Una engine and rebuild BuildTool.")
        null
    }
}
// What the variable Project.json names for a setting holds. Passwords go from here to the signing config only.
fun signingVariable(setting: String): String? =
    signingSetting("--signing-env", setting)?.let { System.getenv(it) }?.ifEmpty { null }

// The native library, built by Una's BuildTool for each ABI before the app is assembled.
val buildNativeTasks = marconiAbis.map { abi ->
    tasks.register<Exec>("buildMarconiNative_$abi") {
        description = "Builds libMarconi.so for $abi with the Una BuildTool"
        workingDir = marconiRoot
        if (isWindows) {
            commandLine("powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "Build.ps1",
                "-Project", "Apps/Android/Project.json", "-Config", marconiConfig, "--platform", "Android", "--abi", abi)
        } else {
            commandLine("bash", "Build.sh", "Apps/Android/Project.json", "-config", marconiConfig,
                "--platform", "Android", "--abi", abi)
        }
    }
}
// BuildTool writes Build/Marconi/Android/<Config>/libMarconi.so for arm64-v8a and Build/Marconi/Android-<abi>/<Config>/
// for the other ABIs; the app wants them as <abi>/libMarconi.so, so they are copied into one folder.
val jniLibsDir: File = layout.buildDirectory.dir("marconiJniLibs").get().asFile
val buildMarconiNative = tasks.register<Copy>("buildMarconiNative") {
    description = "Builds libMarconi.so for every ABI in marconiAbis and gathers them for the app"
    dependsOn(buildNativeTasks)
    for (abi in marconiAbis) {
        val folder = if (abi == "arm64-v8a") "Android" else "Android-$abi"
        from(File(marconiRoot, "Build/Marconi/$folder/$marconiConfig/libMarconi.so")) { into(abi) }
    }
    into(jniLibsDir)
}
// Every native function NativeSession.kt declares must exist in the library under its JNI name, or the app dies
// the first time it is called. Checked here, with the NDK's llvm-nm, rather than on a phone.
val checkMarconiNative = tasks.register("checkMarconiNative") {
    description = "Checks that libMarconi.so exports every native function NativeSession.kt declares"
    dependsOn(buildMarconiNative)
    doLast {
        val ndk = listOfNotNull(System.getenv("ANDROID_NDK_HOME"), System.getenv("ANDROID_NDK_ROOT"), System.getenv("ANDROID_NDK"))
            .map { File(it) }.firstOrNull { it.isDirectory }
            ?: listOfNotNull(System.getenv("ANDROID_HOME"), System.getenv("ANDROID_SDK_ROOT")).map { File(it, "ndk") }
                .filter { it.isDirectory }.flatMap { it.listFiles()?.toList() ?: emptyList() }.maxByOrNull { it.name }
        val prebuilt = ndk?.let { File(it, "toolchains/llvm/prebuilt").listFiles()?.firstOrNull() }
        val nm = prebuilt?.let { File(it, if (isWindows) "bin/llvm-nm.exe" else "bin/llvm-nm") }
        if (nm == null || !nm.exists()) {
            logger.warn("checkMarconiNative: llvm-nm not found, skipping the native name check")
            return@doLast
        }
        val declared = Regex("external fun ([A-Za-z0-9_]+)").findAll(File(projectDir, "src/main/java/com/redmarmosetstudios/marconi/NativeSession.kt").readText())
            .map { it.groupValues[1] }.toSet()
        for (abi in marconiAbis) {
            val library = File(jniLibsDir, "$abi/libMarconi.so")
            val output = ByteArrayOutputStream()
            project.exec { commandLine(nm.absolutePath, "-D", library.absolutePath); standardOutput = output }
            val exported = Regex("Java_com_redmarmosetstudios_marconi_NativeSession_([A-Za-z0-9_]+)").findAll(output.toString())
                .map { it.groupValues[1] }.toSet()
            val missing = declared - exported
            if (missing.isNotEmpty()) throw GradleException("libMarconi.so ($abi) lacks native functions NativeSession.kt declares: $missing")
        }
    }
}
tasks.named("preBuild") { dependsOn(checkMarconiNative) }

android {
    namespace = "com.redmarmosetstudios.marconi"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.redmarmosetstudios.marconi"
        minSdk = 24
        targetSdk = 35
        versionCode = marconiRevision
        versionName = "$marconiVersion.$marconiRevision"
        ndk {
            abiFilters += marconiAbis
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.srcDirs(jniLibsDir)
        }
    }

    // A release APK must be signed or Android won't install it. With the keystore variable Project.json names
    // set (MARCONI_ANDROID_KEYSTORE; see Apps/Android/Package.sh) it is signed with that key; otherwise with the
    // standard debug key, which installs for testing but isn't for publishing.
    signingConfigs {
        val keystore = signingVariable("android.keystore")
        if (keystore != null) {
            val keystorePassword = signingVariable("android.keystorePassword") ?: ""
            create("release") {
                storeFile = file(keystore)
                storePassword = keystorePassword
                keyAlias = signingSetting("--signing-value", "android.keyAlias") ?: signingVariable("android.keyAlias") ?: "marconi"
                keyPassword = signingVariable("android.keyPassword") ?: keystorePassword
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    packaging {
        jniLibs {
            // The library is already 16 KiB aligned and must stay uncompressed to load straight from the APK
            useLegacyPackaging = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
}
