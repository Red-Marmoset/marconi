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

// The native library, built by Una's BuildTool for each ABI before the app is assembled.
val isWindows = System.getProperty("os.name").lowercase().contains("windows")
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
tasks.named("preBuild") { dependsOn(buildMarconiNative) }

android {
    namespace = "com.redmarmoset.marconi"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.redmarmoset.marconi"
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

    buildTypes {
        release {
            isMinifyEnabled = false
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
}
