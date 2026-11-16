pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        // CodeLocator 依赖的 RestrictionBypass 只发布在 JitPack；限定分组，其他依赖不去 JitPack 查
        maven("https://jitpack.io") {
            content { includeGroup("com.github.ChickenHook") }
        }
    }
}

rootProject.name = "LearnFFmpeg"
include(":app")
include(":lib_av")
