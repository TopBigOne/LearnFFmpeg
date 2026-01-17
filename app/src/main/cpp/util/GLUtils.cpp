/**
 * @file GLUtils.cpp
 * @brief OpenGL ES 工具类实现文件
 * @details 实现着色器编译、程序链接等OpenGL ES相关的工具函数
 */

#include "GLUtils.h"
#include "LogUtil.h"
#include <stdlib.h>
#include <cstring>
#include <GLES2/gl2ext.h>

/**
 * @brief 加载并编译着色器
 * @details 创建着色器对象,加载源代码,编译并检查编译结果
 *          如果编译失败,会输出错误日志并返回0
 * @param shaderType 着色器类型(GL_VERTEX_SHADER或GL_FRAGMENT_SHADER)
 * @param pSource 着色器源代码字符串
 * @return 成功返回着色器对象ID,失败返回0
 */
GLuint GLUtils::LoadShader(GLenum shaderType, const char *pSource)
{
    GLuint shader = 0;
	FUN_BEGIN_TIME("GLUtils::LoadShader")  // 性能计时开始
        shader = glCreateShader(shaderType);  // 创建着色器对象
        if (shader)
        {
            glShaderSource(shader, 1, &pSource, NULL);  // 加载着色器源代码
            glCompileShader(shader);  // 编译着色器
            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);  // 获取编译状态
            if (!compiled)  // 编译失败
            {
                GLint infoLen = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);  // 获取错误日志长度
                if (infoLen)
                {
                    char* buf = (char*) malloc((size_t)infoLen);
                    if (buf)
                    {
                        glGetShaderInfoLog(shader, infoLen, NULL, buf);  // 获取错误日志
                        LOGCATE("GLUtils::LoadShader Could not compile shader %d:\n%s\n", shaderType, buf);
                        free(buf);
                    }
                    glDeleteShader(shader);  // 删除编译失败的着色器
                    shader = 0;
                }
            }
        }
	FUN_END_TIME("GLUtils::LoadShader")  // 性能计时结束
	return shader;
}

/**
 * @brief 创建OpenGL着色器程序
 * @details 加载并编译顶点着色器和片段着色器,创建程序对象,链接着色器
 *          链接完成后会分离并删除着色器对象
 *          如果链接失败,会输出错误日志并返回0
 * @param pVertexShaderSource 顶点着色器源代码
 * @param pFragShaderSource 片段着色器源代码
 * @param vertexShaderHandle 返回顶点着色器句柄(输出参数)
 * @param fragShaderHandle 返回片段着色器句柄(输出参数)
 * @return 成功返回程序对象ID,失败返回0
 */
GLuint GLUtils::CreateProgram(const char *pVertexShaderSource, const char *pFragShaderSource, GLuint &vertexShaderHandle, GLuint &fragShaderHandle)
{
    GLuint program = 0;
    FUN_BEGIN_TIME("GLUtils::CreateProgram")  // 性能计时开始
        vertexShaderHandle = LoadShader(GL_VERTEX_SHADER, pVertexShaderSource);  // 编译顶点着色器
        if (!vertexShaderHandle) return program;
        fragShaderHandle = LoadShader(GL_FRAGMENT_SHADER, pFragShaderSource);  // 编译片段着色器
        if (!fragShaderHandle) return program;

        program = glCreateProgram();  // 创建程序对象
        if (program)
        {
            glAttachShader(program, vertexShaderHandle);  // 附加顶点着色器
            CheckGLError("glAttachShader");
            glAttachShader(program, fragShaderHandle);  // 附加片段着色器
            CheckGLError("glAttachShader");
            glLinkProgram(program);  // 链接程序
            GLint linkStatus = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);  // 获取链接状态

            glDetachShader(program, vertexShaderHandle);  // 分离顶点着色器
            glDeleteShader(vertexShaderHandle);  // 删除顶点着色器
            vertexShaderHandle = 0;
            glDetachShader(program, fragShaderHandle);  // 分离片段着色器
            glDeleteShader(fragShaderHandle);  // 删除片段着色器
            fragShaderHandle = 0;
            if (linkStatus != GL_TRUE)  // 链接失败
            {
                GLint bufLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);  // 获取错误日志长度
                if (bufLength)
                {
                    char* buf = (char*) malloc((size_t)bufLength);
                    if (buf)
                    {
                        glGetProgramInfoLog(program, bufLength, NULL, buf);  // 获取错误日志
                        LOGCATE("GLUtils::CreateProgram Could not link program:\n%s\n", buf);
                        free(buf);
                    }
                }
                glDeleteProgram(program);  // 删除链接失败的程序
                program = 0;
            }
        }
    FUN_END_TIME("GLUtils::CreateProgram")  // 性能计时结束
    LOGCATE("GLUtils::CreateProgram program = %d", program);
	return program;
}

/**
 * @brief 创建支持Transform Feedback的OpenGL着色器程序
 * @details 与CreateProgram类似,但在链接前配置Transform Feedback
 *          Transform Feedback允许将顶点着色器的输出捕获到缓冲区对象中
 * @param pVertexShaderSource 顶点着色器源代码
 * @param pFragShaderSource 片段着色器源代码
 * @param vertexShaderHandle 返回顶点着色器句柄(输出参数)
 * @param fragShaderHandle 返回片段着色器句柄(输出参数)
 * @param varying 要捕获的varying变量名称数组
 * @param varyingCount varying变量的数量
 * @return 成功返回程序对象ID,失败返回0
 */
GLuint GLUtils::CreateProgramWithFeedback(const char *pVertexShaderSource, const char *pFragShaderSource, GLuint &vertexShaderHandle, GLuint &fragShaderHandle, GLchar const **varying, int varyingCount)
{
    GLuint program = 0;
    FUN_BEGIN_TIME("GLUtils::CreateProgramWithFeedback")  // 性能计时开始
        vertexShaderHandle = LoadShader(GL_VERTEX_SHADER, pVertexShaderSource);  // 编译顶点着色器
        if (!vertexShaderHandle) return program;

        fragShaderHandle = LoadShader(GL_FRAGMENT_SHADER, pFragShaderSource);  // 编译片段着色器
        if (!fragShaderHandle) return program;

        program = glCreateProgram();  // 创建程序对象
        if (program)
        {
            glAttachShader(program, vertexShaderHandle);  // 附加顶点着色器
            CheckGLError("glAttachShader");
            glAttachShader(program, fragShaderHandle);  // 附加片段着色器
            CheckGLError("glAttachShader");

            // 配置Transform Feedback - 指定要捕获的varying变量
            // GL_INTERLEAVED_ATTRIBS表示所有变量交错存储在一个缓冲区中
            glTransformFeedbackVaryings(program, varyingCount, varying, GL_INTERLEAVED_ATTRIBS);
            GO_CHECK_GL_ERROR();

            glLinkProgram(program);  // 链接程序
            GLint linkStatus = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);  // 获取链接状态

            glDetachShader(program, vertexShaderHandle);  // 分离顶点着色器
            glDeleteShader(vertexShaderHandle);  // 删除顶点着色器
            vertexShaderHandle = 0;
            glDetachShader(program, fragShaderHandle);  // 分离片段着色器
            glDeleteShader(fragShaderHandle);  // 删除片段着色器
            fragShaderHandle = 0;
            if (linkStatus != GL_TRUE)  // 链接失败
            {
                GLint bufLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);  // 获取错误日志长度
                if (bufLength)
                {
                    char* buf = (char*) malloc((size_t)bufLength);
                    if (buf)
                    {
                        glGetProgramInfoLog(program, bufLength, NULL, buf);  // 获取错误日志
                        LOGCATE("GLUtils::CreateProgramWithFeedback Could not link program:\n%s\n", buf);
                        free(buf);
                    }
                }
                glDeleteProgram(program);  // 删除链接失败的程序
                program = 0;
            }
        }
    FUN_END_TIME("GLUtils::CreateProgramWithFeedback")  // 性能计时结束
    LOGCATE("GLUtils::CreateProgramWithFeedback program = %d", program);
    return program;
}

/**
 * @brief 删除OpenGL着色器程序
 * @details 解绑当前程序并删除程序对象,释放相关资源
 * @param program 要删除的程序对象ID引用,删除后将被置为0
 */
void GLUtils::DeleteProgram(GLuint &program)
{
    LOGCATE("GLUtils::DeleteProgram");
    if (program)
    {
        glUseProgram(0);  // 解绑当前程序
        glDeleteProgram(program);  // 删除程序对象
        program = 0;
    }
}

/**
 * @brief 检查OpenGL错误
 * @details 循环获取所有待处理的OpenGL错误并输出到日志
 *          用于调试OpenGL调用是否正确
 * @param pGLOperation 执行的OpenGL操作名称,用于错误日志标识
 */
void GLUtils::CheckGLError(const char *pGLOperation)
{
    for (GLint error = glGetError(); error; error = glGetError())  // 循环获取所有错误
    {
        LOGCATE("GLUtils::CheckGLError GL Operation %s() glError (0x%x)\n", pGLOperation, error);
    }

}

/**
 * @brief 创建OpenGL着色器程序(简化版本)
 * @details 内部调用CreateProgram的完整版本,但不返回着色器句柄
 *          适用于不需要保留着色器句柄的场景
 * @param pVertexShaderSource 顶点着色器源代码
 * @param pFragShaderSource 片段着色器源代码
 * @return 成功返回程序对象ID,失败返回0
 */
GLuint GLUtils::CreateProgram(const char *pVertexShaderSource, const char *pFragShaderSource) {
    GLuint vertexShaderHandle, fragShaderHandle;
    return CreateProgram(pVertexShaderSource, pFragShaderSource, vertexShaderHandle, fragShaderHandle);
}
