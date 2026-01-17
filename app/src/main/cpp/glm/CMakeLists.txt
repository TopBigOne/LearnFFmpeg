# 设置工程名称变量
set(NAME glm_dummy)

# 查找当前目录下所有 .cpp 文件，存储到 ROOT_SOURCE 变量
file(GLOB ROOT_SOURCE *.cpp)
# 查找当前目录下所有 .inl 文件，存储到 ROOT_INLINE 变量
file(GLOB ROOT_INLINE *.inl)
# 查找当前目录下所有 .hpp 文件，存储到 ROOT_HEADER 变量
file(GLOB ROOT_HEADER *.hpp)
# 查找上一级目录下所有 .txt 文件，存储到 ROOT_TEXT 变量
file(GLOB ROOT_TEXT ../*.txt)
# 查找上一级 util 目录下的 glm.natvis 文件，存储到 ROOT_NAT 变量
file(GLOB ROOT_NAT ../util/glm.natvis)

# 递归查找 detail 目录下所有 .cpp 文件
file(GLOB_RECURSE CORE_SOURCE ./detail/*.cpp)
# 递归查找 detail 目录下所有 .inl 文件
file(GLOB_RECURSE CORE_INLINE ./detail/*.inl)
# 递归查找 detail 目录下所有 .hpp 文件
file(GLOB_RECURSE CORE_HEADER ./detail/*.hpp)

# 递归查找 gtc 目录下所有 .cpp 文件
file(GLOB_RECURSE GTC_SOURCE ./gtc/*.cpp)
# 递归查找 gtc 目录下所有 .inl 文件
file(GLOB_RECURSE GTC_INLINE ./gtc/*.inl)
# 递归查找 gtc 目录下所有 .hpp 文件
file(GLOB_RECURSE GTC_HEADER ./gtc/*.hpp)

# 递归查找 gtx 目录下所有 .cpp 文件
file(GLOB_RECURSE GTX_SOURCE ./gtx/*.cpp)
# 递归查找 gtx 目录下所有 .inl 文件
file(GLOB_RECURSE GTX_INLINE ./gtx/*.inl)
# 递归查找 gtx 目录下所有 .hpp 文件
file(GLOB_RECURSE GTX_HEADER ./gtx/*.hpp)

# 把上一级目录下的 .txt 文件归为 "Text Files" 分组（用于 IDE 中分组显示）
source_group("Text Files" FILES ${ROOT_TEXT})
# 以下分别将 core、gtc、gtx 目录下的源文件、内联文件、头文件分组
source_group("Core Files" FILES ${CORE_SOURCE})
source_group("Core Files" FILES ${CORE_INLINE})
source_group("Core Files" FILES ${CORE_HEADER})
source_group("GTC Files" FILES ${GTC_SOURCE})
source_group("GTC Files" FILES ${GTC_INLINE})
source_group("GTC Files" FILES ${GTC_HEADER})
source_group("GTX Files" FILES ${GTX_SOURCE})
source_group("GTX Files" FILES ${GTX_INLINE})
source_group("GTX Files" FILES ${GTX_HEADER})

# 添加上一级目录为头文件包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# 如果启用了 GLM_TEST_ENABLE 选项，则添加一个可执行文件，包含所有上面收集到的源文件
if(GLM_TEST_ENABLE)
	add_executable(${NAME} ${ROOT_TEXT} ${ROOT_NAT}
			${ROOT_SOURCE}    ${ROOT_INLINE}    ${ROOT_HEADER}
			${CORE_SOURCE}    ${CORE_INLINE}    ${CORE_HEADER}
			${GTC_SOURCE}     ${GTC_INLINE}     ${GTC_HEADER}
			${GTX_SOURCE}     ${GTX_INLINE}     ${GTX_HEADER})
endif(GLM_TEST_ENABLE)

# 下面两行是被注释掉的示例，演示如何生成静态库或动态库
#add_library(glm STATIC glm.cpp)
#add_library(glm_shared SHARED glm.cpp)
