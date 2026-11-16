import org.gradle.kotlin.dsl.debugImplementation

/**
 * 从 NDK 取出 AddressSanitizer 运行时（libclang_rt.asan-*-android.so）和启动脚本 wrap.sh，
 * 作为 asan 构建类型的 jniLibs / Java 资源打进 APK。不把 NDK 里的二进制提交进仓库，NDK 升级后也自动跟上。
 */
abstract class PrepareAsanRuntimeTask : DefaultTask() {
    @get:Input
    abstract val ndkPath: Property<String>

    @get:Input
    abstract val abis: ListProperty<String>

    @get:OutputDirectory
    abstract val jniLibsDir: DirectoryProperty

    @get:OutputDirectory
    abstract val resourcesDir: DirectoryProperty

    @TaskAction
    fun prepare() {
        val ndk = File(ndkPath.get())
        val wrapSh = File(ndk, "wrap.sh/asan.sh")
        check(wrapSh.isFile) { "NDK 中找不到 $wrapSh" }
        val runtimes = File(ndk, "toolchains/llvm/prebuilt").walkTopDown()
            .filter { it.isFile && it.name.startsWith("libclang_rt.asan-") && it.name.endsWith("-android.so") }
            .toList()
        val archOfAbi = mapOf("arm64-v8a" to "aarch64", "armeabi-v7a" to "arm", "x86" to "i686", "x86_64" to "x86_64")
        jniLibsDir.get().asFile.deleteRecursively()
        resourcesDir.get().asFile.deleteRecursively()
        for (abi in abis.get()) {
            val arch = archOfAbi.getValue(abi)
            val runtime = runtimes.firstOrNull { it.name == "libclang_rt.asan-$arch-android.so" }
                ?: error("NDK 中找不到 $abi 的 ASan 运行时（libclang_rt.asan-$arch-android.so）")
            runtime.copyTo(jniLibsDir.get().dir(abi).file(runtime.name).asFile, overwrite = true)
            // wrap.sh 必须放在 APK 的 lib/<abi>/ 下，系统启动进程时用它预加载 ASan 运行时。
            // 在 NDK 脚本的 ASAN_OPTIONS 上加 color=never：报告输出到 logcat，不需要终端颜色控制符
            val script = wrapSh.readText().replace("allow_user_segv_handler=1", "allow_user_segv_handler=1,color=never")
            val target = resourcesDir.get().dir("lib/$abi").file("wrap.sh").asFile
            target.parentFile.mkdirs()
            target.writeText(script)
        }
    }
}

plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.byteflow.learnffmpeg"
    ndkVersion = "28.2.13676358"   // 与 lib_av 一致：asan 构建从这个 NDK 取 ASan 运行时
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "com.byteflow.learnffmpeg"
        minSdk = 23
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // native 库只编译了这两个 ABI（见 lib_av），APK 也只打包这两个
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
        // AddressSanitizer 构建：./gradlew :app:installAsan（说明见 lib_av/src/main/cpp/util/ASAN.md）
        create("asan") {
            initWith(getByName("debug"))
            isDebuggable = true   // wrap.sh 只对可调试的应用生效
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

androidComponents {
    onVariants(selector().withBuildType("asan")) { variant ->
        val prepare = tasks.register<PrepareAsanRuntimeTask>("prepare${variant.name.replaceFirstChar { it.uppercase() }}Runtime") {
            ndkPath.set(androidComponents.sdkComponents.sdkDirectory.map { it.dir("ndk/${android.ndkVersion}").asFile.absolutePath })
            abis.set(listOf("arm64-v8a", "armeabi-v7a"))
        }
        variant.sources.jniLibs?.addGeneratedSourceDirectory(prepare, PrepareAsanRuntimeTask::jniLibsDir)
        variant.sources.resources?.addGeneratedSourceDirectory(prepare, PrepareAsanRuntimeTask::resourcesDir)
        // wrap.sh 要求 native 库解压到磁盘（extractNativeLibs），不能直接从 APK 中映射
        variant.packaging.jniLibs.useLegacyPackaging.set(true)
    }
}

dependencies {
    implementation(project(":lib_av"))
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    debugImplementation(libs.codelocator.lancet.all)

}