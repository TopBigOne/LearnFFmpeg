/**
 * @file GLUtils.h
 * @brief OpenGL ES 工具类头文件
 * @details 提供 OpenGL ES 着色器、程序对象的创建、管理和uniform变量设置等工具函数
 */

#ifndef _BYTE_FLOW_GL_UTILS_H_
#define _BYTE_FLOW_GL_UTILS_H_

#include <GLES3/gl3.h>
#include <string>
#include <glm.hpp>

/**
 * @class GLUtils
 * @brief OpenGL ES 工具类
 * @details 提供着色器编译、程序链接、uniform设置、坐标转换等静态工具方法
 */
class GLUtils {
public:
    /**
     * @brief 加载并编译着色器
     * @param shaderType 着色器类型(GL_VERTEX_SHADER 或 GL_FRAGMENT_SHADER)
     * @param pSource 着色器源代码字符串
     * @return 编译成功返回着色器对象ID,失败返回0
     */
    static GLuint LoadShader(GLenum shaderType, const char *pSource);

    /**
     * @brief 创建着色器程序(带着色器句柄返回)
     * @param pVertexShaderSource 顶点着色器源代码
     * @param pFragShaderSource 片段着色器源代码
     * @param vertexShaderHandle 返回顶点着色器句柄(输出参数)
     * @param fragShaderHandle 返回片段着色器句柄(输出参数)
     * @return 成功返回程序对象ID,失败返回0
     */
    static GLuint CreateProgram(const char *pVertexShaderSource, const char *pFragShaderSource,
                                GLuint &vertexShaderHandle,
                                GLuint &fragShaderHandle);

    /**
     * @brief 创建着色器程序(简化版本)
     * @param pVertexShaderSource 顶点着色器源代码
     * @param pFragShaderSource 片段着色器源代码
     * @return 成功返回程序对象ID,失败返回0
     */
    static GLuint CreateProgram(const char *pVertexShaderSource, const char *pFragShaderSource);

    /**
     * @brief 创建支持Transform Feedback的着色器程序
     * @param pVertexShaderSource 顶点着色器源代码
     * @param pFragShaderSource 片段着色器源代码
     * @param vertexShaderHandle 返回顶点着色器句柄(输出参数)
     * @param fragShaderHandle 返回片段着色器句柄(输出参数)
     * @param varying 要捕获的varying变量名称数组
     * @param varyingCount varying变量的数量
     * @return 成功返回程序对象ID,失败返回0
     */
    static GLuint CreateProgramWithFeedback(
            const char *pVertexShaderSource,
            const char *pFragShaderSource,
            GLuint &vertexShaderHandle,
            GLuint &fragShaderHandle,
            const GLchar **varying,
            int varyingCount);

    /**
     * @brief 删除着色器程序
     * @param program 要删除的程序对象ID引用
     */
    static void DeleteProgram(GLuint &program);

    /**
     * @brief 检查OpenGL错误
     * @param pGLOperation 执行的OpenGL操作名称(用于日志输出)
     */
    static void CheckGLError(const char *pGLOperation);

    /**
     * @brief 设置bool类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value bool值
     */
    static void setBool(GLuint programId, const std::string &name, bool value) {
        glUniform1i(glGetUniformLocation(programId, name.c_str()), (int) value);
    }

    /**
     * @brief 设置int类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value int值
     */
    static void setInt(GLuint programId, const std::string &name, int value) {
        glUniform1i(glGetUniformLocation(programId, name.c_str()), value);
    }

    /**
     * @brief 设置float类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value float值
     */
    static void setFloat(GLuint programId, const std::string &name, float value) {
        glUniform1f(glGetUniformLocation(programId, name.c_str()), value);
    }

    /**
     * @brief 设置vec2类型的uniform变量(向量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value 二维向量值
     */
    static void setVec2(GLuint programId, const std::string &name, const glm::vec2 &value) {
        glUniform2fv(glGetUniformLocation(programId, name.c_str()), 1, &value[0]);
    }

    /**
     * @brief 设置vec2类型的uniform变量(分量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param x x分量
     * @param y y分量
     */
    static void setVec2(GLuint programId, const std::string &name, float x, float y) {
        glUniform2f(glGetUniformLocation(programId, name.c_str()), x, y);
    }

    /**
     * @brief 设置vec3类型的uniform变量(向量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value 三维向量值
     */
    static void setVec3(GLuint programId, const std::string &name, const glm::vec3 &value) {
        glUniform3fv(glGetUniformLocation(programId, name.c_str()), 1, &value[0]);
    }

    /**
     * @brief 设置vec3类型的uniform变量(分量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param x x分量
     * @param y y分量
     * @param z z分量
     */
    static void setVec3(GLuint programId, const std::string &name, float x, float y, float z) {
        glUniform3f(glGetUniformLocation(programId, name.c_str()), x, y, z);
    }

    /**
     * @brief 设置vec4类型的uniform变量(向量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param value 四维向量值
     */
    static void setVec4(GLuint programId, const std::string &name, const glm::vec4 &value) {
        glUniform4fv(glGetUniformLocation(programId, name.c_str()), 1, &value[0]);
    }

    /**
     * @brief 设置vec4类型的uniform变量(分量形式)
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param x x分量
     * @param y y分量
     * @param z z分量
     * @param w w分量
     */
    static void setVec4(GLuint programId, const std::string &name, float x, float y, float z, float w) {
        glUniform4f(glGetUniformLocation(programId, name.c_str()), x, y, z, w);
    }

    /**
     * @brief 设置mat2类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param mat 2x2矩阵
     */
    static void setMat2(GLuint programId, const std::string &name, const glm::mat2 &mat) {
        glUniformMatrix2fv(glGetUniformLocation(programId, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    /**
     * @brief 设置mat3类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param mat 3x3矩阵
     */
    static void setMat3(GLuint programId, const std::string &name, const glm::mat3 &mat) {
        glUniformMatrix3fv(glGetUniformLocation(programId, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    /**
     * @brief 设置mat4类型的uniform变量
     * @param programId 着色器程序ID
     * @param name uniform变量名称
     * @param mat 4x4矩阵
     */
    static void setMat4(GLuint programId, const std::string &name, const glm::mat4 &mat) {
        glUniformMatrix4fv(glGetUniformLocation(programId, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    /**
     * @brief 纹理坐标转换为顶点坐标
     * @details 将纹理坐标空间[0,1]转换为OpenGL顶点坐标空间[-1,1]
     *          x: [0,1] -> [-1,1]
     *          y: [0,1] -> [1,-1] (翻转Y轴)
     * @param texCoord 纹理坐标(取值范围[0,1])
     * @return 顶点坐标(取值范围[-1,1])
     */
    static glm::vec3 texCoordToVertexCoord(glm::vec2 texCoord) {
        return glm::vec3(2 * texCoord.x - 1, 1 - 2 * texCoord.y, 0);
    }

};

#endif // _BYTE_FLOW_GL_UTILS_H_