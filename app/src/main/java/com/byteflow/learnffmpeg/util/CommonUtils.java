package com.byteflow.learnffmpeg.util;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public class CommonUtils {
    private static final String TAG = "CommonUtils";
    private static final String TEMP_SUFFIX = ".tmp";

    /** 拷贝在这个单线程里串行执行，不占用主线程 */
    private static final ExecutorService COPY_EXECUTOR = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "asset-copier");
        t.setPriority(Thread.MIN_PRIORITY);
        return t;
    });
    /** 正在拷贝时，重复的请求（例如 onResume 多次触发）直接忽略 */
    private static final AtomicBoolean sCopying = new AtomicBoolean(false);

    public interface CopyCallback {
        /** 在主线程回调。copied：本次实际拷贝的文件数；failed：失败的文件数 */
        void onCopyFinished(int copied, int failed);
    }

    /**
     * 在后台线程把 assets 下的目录拷贝到 destParent 下（例如 assets/byteflow → destParent/byteflow）。
     *
     * 每个文件：
     *  - 已存在且大小与 asset 一致 → 跳过；
     *  - 否则先写到 "文件名.tmp"，写完并核对大小后再改名为正式文件名。
     *    拷贝中途进程被杀，只会留下 .tmp，下次会重新拷贝，不会出现"残缺文件被当成已拷贝"的情况。
     *
     * 大小核对依赖 {@link AssetManager#openFd}，只对未压缩的 asset 有效（mp4 等媒体文件默认不压缩）；
     * 被压缩的 asset 拿不到原始大小，退化为"非空即视为已拷贝"。
     */
    public static void copyAssetsDirAsync(@NonNull Context context, @NonNull String assetsDirName,
                                          @NonNull File destParent, @Nullable CopyCallback callback) {
        if (!sCopying.compareAndSet(false, true)) {
            Log.d(TAG, "copyAssetsDirAsync: already copying, skip");
            return;
        }
        final AssetManager assets = context.getApplicationContext().getAssets();
        final Handler mainHandler = new Handler(Looper.getMainLooper());
        COPY_EXECUTOR.execute(() -> {
            int[] result = {0, 0};   // copied, failed
            try {
                copyAsset(assets, assetsDirName, new File(destParent, lastSegment(assetsDirName)), result);
            } finally {
                sCopying.set(false);
            }
            Log.d(TAG, "copyAssetsDirAsync: done, copied=" + result[0] + ", failed=" + result[1]);
            if (callback != null) {
                mainHandler.post(() -> callback.onCopyFinished(result[0], result[1]));
            }
        });
    }

    /** assetPath 是目录则递归，是文件则拷贝到 dest */
    private static void copyAsset(AssetManager assets, String assetPath, File dest, int[] result) {
        String[] children;
        try {
            children = assets.list(assetPath);
        } catch (IOException e) {
            Log.e(TAG, "list assets fail: " + assetPath, e);
            result[1]++;
            return;
        }
        if (children == null || children.length == 0) {
            copyFile(assets, assetPath, dest, result);
            return;
        }
        if (!dest.exists() && !dest.mkdirs()) {
            Log.e(TAG, "mkdirs fail: " + dest);
            result[1]++;
            return;
        }
        for (String child : children) {
            copyAsset(assets, assetPath + "/" + child, new File(dest, child), result);
        }
    }

    private static void copyFile(AssetManager assets, String assetPath, File dest, int[] result) {
        long expectedSize = assetSize(assets, assetPath);
        if (dest.exists()) {
            boolean complete = expectedSize >= 0 ? dest.length() == expectedSize : dest.length() > 0;
            if (complete) return;
            Log.w(TAG, "incomplete file, recopy: " + dest + " (" + dest.length() + " / " + expectedSize + ")");
        }

        File temp = new File(dest.getPath() + TEMP_SUFFIX);
        try (InputStream in = assets.open(assetPath);
             FileOutputStream out = new FileOutputStream(temp)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) != -1) {
                out.write(buffer, 0, n);
            }
            out.getFD().sync();   // 确保数据落盘后再改名
        } catch (IOException e) {
            Log.e(TAG, "copy fail: " + assetPath, e);
            temp.delete();
            result[1]++;
            return;
        }

        if (expectedSize >= 0 && temp.length() != expectedSize) {
            Log.e(TAG, "size mismatch: " + assetPath + " " + temp.length() + " / " + expectedSize);
            temp.delete();
            result[1]++;
            return;
        }
        if (dest.exists() && !dest.delete()) {
            Log.e(TAG, "delete old file fail: " + dest);
            temp.delete();
            result[1]++;
            return;
        }
        if (!temp.renameTo(dest)) {
            Log.e(TAG, "rename fail: " + temp + " -> " + dest);
            temp.delete();
            result[1]++;
            return;
        }
        result[0]++;
    }

    /** asset 原始大小；被压缩的 asset 无法 openFd，返回 -1 */
    private static long assetSize(AssetManager assets, String assetPath) {
        try (AssetFileDescriptor fd = assets.openFd(assetPath)) {
            return fd.getLength();
        } catch (IOException e) {
            return -1;
        }
    }

    private static String lastSegment(String path) {
        int i = path.lastIndexOf('/');
        return i >= 0 ? path.substring(i + 1) : path;
    }
}
