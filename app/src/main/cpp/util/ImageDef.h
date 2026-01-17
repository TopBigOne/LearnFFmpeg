/**
 * @file ImageDef.h
 * @brief 图像数据结构和工具定义
 * @details 定义了常用的图像格式、图像数据结构以及图像操作的工具类
 * @author 公众号:字节流动
 * @date 2019/7/10
 */

#ifndef NDK_OPENGLES_3_0_IMAGEDEF_H
#define NDK_OPENGLES_3_0_IMAGEDEF_H

#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include "stdio.h"
#include "sys/stat.h"
#include "stdint.h"
#include "LogUtil.h"

/** @brief RGBA格式标识 */
#define IMAGE_FORMAT_RGBA           0x01
/** @brief NV21格式标识(YUV420SP,VU交错) */
#define IMAGE_FORMAT_NV21           0x02
/** @brief NV12格式标识(YUV420SP,UV交错) */
#define IMAGE_FORMAT_NV12           0x03
/** @brief I420格式标识(YUV420P,平面格式) */
#define IMAGE_FORMAT_I420           0x04

/** @brief RGBA格式扩展名 */
#define IMAGE_FORMAT_RGBA_EXT       "RGB32"
/** @brief NV21格式扩展名 */
#define IMAGE_FORMAT_NV21_EXT       "NV21"
/** @brief NV12格式扩展名 */
#define IMAGE_FORMAT_NV12_EXT       "NV12"
/** @brief I420格式扩展名 */
#define IMAGE_FORMAT_I420_EXT       "I420"

/**
 * @struct RectF
 * @brief 浮点型矩形区域结构体
 * @details 用于表示图像中的矩形区域,坐标值为浮点数
 */
typedef struct _tag_NativeRectF
{
	float left;    ///< 左边界坐标
	float top;     ///< 上边界坐标
	float right;   ///< 右边界坐标
	float bottom;  ///< 下边界坐标

	/** @brief 构造函数,初始化所有坐标为0 */
	_tag_NativeRectF()
	{
		left = top = right = bottom = 0.0f;
	}
} RectF;

/**
 * @struct NativeImage
 * @brief 原生图像数据结构
 * @details 存储图像的宽高、格式以及各平面的数据指针和行宽
 *          支持多种YUV格式和RGBA格式
 */
typedef struct _tag_NativeImage
{
	int width;           ///< 图像宽度(像素)
	int height;          ///< 图像高度(像素)
	int format;          ///< 图像格式(IMAGE_FORMAT_XXX)
	uint8_t *ppPlane[3]; ///< 图像数据平面指针数组(最多3个平面)
	int pLineSize[3];    ///< 每个平面的行字节数

	/** @brief 构造函数,初始化所有成员为0或nullptr */
	_tag_NativeImage()
	{
		width = 0;
		height = 0;
		format = 0;
		ppPlane[0] = nullptr;
		ppPlane[1] = nullptr;
		ppPlane[2] = nullptr;
		pLineSize[0] = 0;
		pLineSize[1] = 0;
		pLineSize[2] = 0;
	}
} NativeImage;

/**
 * @class NativeImageUtil
 * @brief 原生图像工具类
 * @details 提供图像内存分配、释放、拷贝、导出等工具函数
 */
class NativeImageUtil
{
public:
	/**
	 * @brief 为图像分配内存
	 * @details 根据图像格式和尺寸分配对应的内存空间
	 *          - RGBA: 单平面,每像素4字节
	 *          - NV21/NV12: 两平面,Y平面+UV交错平面
	 *          - I420: 三平面,Y平面+U平面+V平面
	 * @param pImage 要分配内存的图像结构指针
	 */
	static void AllocNativeImage(NativeImage *pImage)
	{
		if (pImage->height == 0 || pImage->width == 0) return;

		switch (pImage->format)
		{
			case IMAGE_FORMAT_RGBA:
			{
				// RGBA格式: width * height * 4字节
				pImage->ppPlane[0] = static_cast<uint8_t *>(malloc(pImage->width * pImage->height * 4));
				pImage->pLineSize[0] = pImage->width * 4;
				pImage->pLineSize[1] = 0;
				pImage->pLineSize[2] = 0;
			}
				break;
			case IMAGE_FORMAT_NV12:
			case IMAGE_FORMAT_NV21:
			{
				// NV12/NV21格式: width * height * 1.5字节
				// Y平面: width * height
				// UV平面: width * height / 2
				pImage->ppPlane[0] = static_cast<uint8_t *>(malloc(pImage->width * pImage->height * 1.5));
				pImage->ppPlane[1] = pImage->ppPlane[0] + pImage->width * pImage->height;
				pImage->pLineSize[0] = pImage->width;
				pImage->pLineSize[1] = pImage->width;
				pImage->pLineSize[2] = 0;
			}
				break;
			case IMAGE_FORMAT_I420:
			{
				// I420格式: width * height * 1.5字节
				// Y平面: width * height
				// U平面: width/2 * height/2
				// V平面: width/2 * height/2
				pImage->ppPlane[0] = static_cast<uint8_t *>(malloc(pImage->width * pImage->height * 1.5));
				pImage->ppPlane[1] = pImage->ppPlane[0] + pImage->width * pImage->height;
				pImage->ppPlane[2] = pImage->ppPlane[1] + (pImage->width >> 1) * (pImage->height >> 1);
				pImage->pLineSize[0] = pImage->width;
				pImage->pLineSize[1] = pImage->width / 2;
				pImage->pLineSize[2] = pImage->width / 2;
			}
				break;
			default:
				LOGCATE("NativeImageUtil::AllocNativeImage do not support the format. Format = %d", pImage->format);
				break;
		}
	}

	/**
	 * @brief 释放图像内存
	 * @details 释放图像数据占用的内存,并将指针置为nullptr
	 *          注意:只需要释放第一个平面的指针,其他平面指向同一块内存
	 * @param pImage 要释放内存的图像结构指针
	 */
	static void FreeNativeImage(NativeImage *pImage)
	{
		if (pImage == nullptr || pImage->ppPlane[0] == nullptr) return;

		free(pImage->ppPlane[0]);  // 释放内存
		pImage->ppPlane[0] = nullptr;
		pImage->ppPlane[1] = nullptr;
		pImage->ppPlane[2] = nullptr;
	}

	/**
	 * @brief 拷贝图像数据
	 * @details 将源图像的数据拷贝到目标图像
	 *          - 两个图像必须具有相同的宽度、高度和格式
	 *          - 如果目标图像未分配内存,会自动分配
	 *          - 支持不同行宽的图像拷贝(逐行拷贝)
	 * @param pSrcImg 源图像指针
	 * @param pDstImg 目标图像指针
	 */
	static void CopyNativeImage(NativeImage *pSrcImg, NativeImage *pDstImg)
	{
	    LOGCATE("NativeImageUtil::CopyNativeImage src[w,h,format]=[%d, %d, %d], dst[w,h,format]=[%d, %d, %d]", pSrcImg->width, pSrcImg->height, pSrcImg->format, pDstImg->width, pDstImg->height, pDstImg->format);
        LOGCATE("NativeImageUtil::CopyNativeImage src[line0,line1,line2]=[%d, %d, %d], dst[line0,line1,line2]=[%d, %d, %d]", pSrcImg->pLineSize[0], pSrcImg->pLineSize[1], pSrcImg->pLineSize[2], pDstImg->pLineSize[0], pDstImg->pLineSize[1], pDstImg->pLineSize[2]);

        if(pSrcImg == nullptr || pSrcImg->ppPlane[0] == nullptr) return;

		// 检查参数有效性:格式、宽高必须一致
		if(pSrcImg->format != pDstImg->format ||
		   pSrcImg->width != pDstImg->width ||
		   pSrcImg->height != pDstImg->height)
		{
			LOGCATE("NativeImageUtil::CopyNativeImage invalid params.");
			return;
		}

		if(pDstImg->ppPlane[0] == nullptr) AllocNativeImage(pDstImg);  // 目标图像未分配则分配内存

		switch (pSrcImg->format)
		{
			case IMAGE_FORMAT_I420:
			{
				// Y平面拷贝
				if(pSrcImg->pLineSize[0] != pDstImg->pLineSize[0]) {
					// 行宽不同,逐行拷贝
					for (int i = 0; i < pSrcImg->height; ++i) {
						memcpy(pDstImg->ppPlane[0] + i * pDstImg->pLineSize[0], pSrcImg->ppPlane[0] + i * pSrcImg->pLineSize[0], pDstImg->width);
					}
				}
				else
				{
					// 行宽相同,整体拷贝
					memcpy(pDstImg->ppPlane[0], pSrcImg->ppPlane[0], pDstImg->pLineSize[0] * pSrcImg->height);
				}

				// U平面拷贝
				if(pSrcImg->pLineSize[1] != pDstImg->pLineSize[1]) {
					for (int i = 0; i < pSrcImg->height / 2; ++i) {
						memcpy(pDstImg->ppPlane[1] + i * pDstImg->pLineSize[1], pSrcImg->ppPlane[1] + i * pSrcImg->pLineSize[1], pDstImg->width / 2);
					}
				}
				else
				{
					memcpy(pDstImg->ppPlane[1], pSrcImg->ppPlane[1], pDstImg->pLineSize[1] * pSrcImg->height / 2);
				}

				// V平面拷贝
				if(pSrcImg->pLineSize[2] != pDstImg->pLineSize[2]) {
					for (int i = 0; i < pSrcImg->height / 2; ++i) {
						memcpy(pDstImg->ppPlane[2] + i * pDstImg->pLineSize[2], pSrcImg->ppPlane[2] + i * pSrcImg->pLineSize[2], pDstImg->width / 2);
					}
				}
				else
				{
					memcpy(pDstImg->ppPlane[2], pSrcImg->ppPlane[2], pDstImg->pLineSize[2] * pSrcImg->height / 2);
				}
			}
			    break;
			case IMAGE_FORMAT_NV21:
			case IMAGE_FORMAT_NV12:
			{
				// Y平面拷贝
				if(pSrcImg->pLineSize[0] != pDstImg->pLineSize[0]) {
					for (int i = 0; i < pSrcImg->height; ++i) {
						memcpy(pDstImg->ppPlane[0] + i * pDstImg->pLineSize[0], pSrcImg->ppPlane[0] + i * pSrcImg->pLineSize[0], pDstImg->width);
					}
				}
				else
				{
					memcpy(pDstImg->ppPlane[0], pSrcImg->ppPlane[0], pDstImg->pLineSize[0] * pSrcImg->height);
				}

				// UV平面拷贝
				if(pSrcImg->pLineSize[1] != pDstImg->pLineSize[1]) {
					for (int i = 0; i < pSrcImg->height / 2; ++i) {
						memcpy(pDstImg->ppPlane[1] + i * pDstImg->pLineSize[1], pSrcImg->ppPlane[1] + i * pSrcImg->pLineSize[1], pDstImg->width);
					}
				}
				else
				{
					memcpy(pDstImg->ppPlane[1], pSrcImg->ppPlane[1], pDstImg->pLineSize[1] * pSrcImg->height / 2);
				}
			}
				break;
			case IMAGE_FORMAT_RGBA:
			{
				if(pSrcImg->pLineSize[0] != pDstImg->pLineSize[0])
				{
					// 行宽不同,逐行拷贝
					for (int i = 0; i < pSrcImg->height; ++i) {
						memcpy(pDstImg->ppPlane[0] + i * pDstImg->pLineSize[0], pSrcImg->ppPlane[0] + i * pSrcImg->pLineSize[0], pDstImg->width * 4);
					}
				} else {
					// 行宽相同,整体拷贝
					memcpy(pDstImg->ppPlane[0], pSrcImg->ppPlane[0], pSrcImg->pLineSize[0] * pSrcImg->height);
				}
			}
				break;
			default:
			{
				LOGCATE("NativeImageUtil::CopyNativeImage do not support the format. Format = %d", pSrcImg->format);
			}
				break;
		}

	}

	/**
	 * @brief 导出图像数据到文件
	 * @details 将图像数据写入指定目录的文件中,文件名自动生成
	 *          文件名格式: IMG_宽x高_自定义名_索引.扩展名
	 *          如果目录不存在,会自动创建
	 * @param pSrcImg 要导出的图像指针
	 * @param pPath 导出目录路径
	 * @param pFileName 自定义文件名(不含扩展名)
	 */
	static void DumpNativeImage(NativeImage *pSrcImg, const char *pPath, const char *pFileName)
	{
		if (pSrcImg == nullptr || pPath == nullptr || pFileName == nullptr) return;

		// 如果目录不存在,创建目录
		if(access(pPath, 0) == -1)
		{
			mkdir(pPath, 0666);
		}

		char imgPath[256] = {0};
		const char *pExt = nullptr;

		// 根据图像格式确定文件扩展名
		switch (pSrcImg->format)
		{
			case IMAGE_FORMAT_I420:
				pExt = IMAGE_FORMAT_I420_EXT;
				break;
			case IMAGE_FORMAT_NV12:
				pExt = IMAGE_FORMAT_NV12_EXT;
				break;
			case IMAGE_FORMAT_NV21:
				pExt = IMAGE_FORMAT_NV21_EXT;
				break;
			case IMAGE_FORMAT_RGBA:
				pExt = IMAGE_FORMAT_RGBA_EXT;
				break;
			default:
				pExt = "Default";
				break;
		}

		static int index = 0;  // 静态变量,用于文件名递增编号
		sprintf(imgPath, "%s/IMG_%dx%d_%s_%d.%s", pPath, pSrcImg->width, pSrcImg->height, pFileName, index, pExt);

		FILE *fp = fopen(imgPath, "wb");

		LOGCATE("DumpNativeImage fp=%p, file=%s", fp, imgPath);

		if(fp)
		{
			// 根据图像格式写入数据
			switch (pSrcImg->format)
			{
				case IMAGE_FORMAT_I420:
				{
					// I420格式: 依次写入Y、U、V平面
					fwrite(pSrcImg->ppPlane[0],
						   static_cast<size_t>(pSrcImg->width * pSrcImg->height), 1, fp);
					fwrite(pSrcImg->ppPlane[1],
						   static_cast<size_t>((pSrcImg->width >> 1) * (pSrcImg->height >> 1)), 1, fp);
					fwrite(pSrcImg->ppPlane[2],
							static_cast<size_t>((pSrcImg->width >> 1) * (pSrcImg->height >> 1)),1,fp);
					break;
				}
				case IMAGE_FORMAT_NV21:
				case IMAGE_FORMAT_NV12:
				{
					// NV12/NV21格式: 依次写入Y平面和UV平面
					fwrite(pSrcImg->ppPlane[0],
						   static_cast<size_t>(pSrcImg->width * pSrcImg->height), 1, fp);
					fwrite(pSrcImg->ppPlane[1],
						   static_cast<size_t>(pSrcImg->width * (pSrcImg->height >> 1)), 1, fp);
					break;
				}
				case IMAGE_FORMAT_RGBA:
				{
					// RGBA格式: 写入完整数据
					fwrite(pSrcImg->ppPlane[0],
						   static_cast<size_t>(pSrcImg->width * pSrcImg->height * 4), 1, fp);
					break;
				}
				default:
				{
					LOGCATE("DumpNativeImage default");
					break;
				}
			}

			fclose(fp);
			fp = NULL;
		}


	}
};


#endif //NDK_OPENGLES_3_0_IMAGEDEF_H
