plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.monday.av"
    ndkVersion = "28.2.13676358"  // r28 默认 16KB 页对齐（Android 15+ 的 16KB 页设备、Google Play 要求）
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        minSdk = 23   // 与 app 保持一致；库的 minSdk 不能高于 app

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.keep")

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")   // prebuilt 下只有这两个 ABI 的 FFmpeg
        }

        externalNativeBuild {
            cmake {
                arguments("-DANDROID_TOOLCHAIN=clang", "-DANDROID_ARM_MODE=arm", "-DANDROID_STL=c++_static")
                cFlags("-ferror-limit=0 -fPIC")
                cppFlags("-std=c++17 -frtti -fexceptions")
            }
        }
    }
    buildTypes {
        // AddressSanitizer 构建：native 代码带 ASan 插桩，用于排查内存错误（说明见 src/main/cpp/util/ASAN.md）。
        // app 有同名构建类型，会自动匹配
        create("asan") {
            initWith(getByName("debug"))
            externalNativeBuild {
                cmake {
                    arguments += "-DLEARNFFMPEG_ASAN=ON"
                }
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

}

dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.core.ktx)
    implementation(libs.material)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
}