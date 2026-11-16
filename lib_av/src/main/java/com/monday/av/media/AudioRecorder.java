package com.monday.av.media;

import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTimestamp;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;

public class AudioRecorder extends Thread {
	private static final String TAG = "AudioRecorder";
	private AudioRecord mAudioRecord = null;
	private static final int DEFAULT_SAMPLE_RATE = 44100;
	private static final int DEFAULT_CHANNEL_LAYOUT = AudioFormat.CHANNEL_IN_STEREO;
	private static final int DEFAULT_SAMPLE_FORMAT = AudioFormat.ENCODING_PCM_16BIT;
	/** 每帧字节数：16bit * 2 声道 */
	private static final int BYTES_PER_FRAME = 2 * 2;
	private static final int READ_BUFFER_SIZE = 4096;
	private static final long NANOS_PER_SECOND = 1_000_000_000L;

	private final AudioRecorderCallback mRecorderCallback;
	/** 已读出的帧数（一帧 = 各声道各一个采样），用于把 AudioTimestamp 换算到每一块的第一个采样 */
	private long mFramesRead = 0;
	private AudioTimestamp mAudioTimestamp;

	public AudioRecorder(AudioRecorderCallback callback) {
		this.mRecorderCallback = callback;
	}

	@Override
	public void run() {

		final int mMinBufferSize = AudioRecord.getMinBufferSize(DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_LAYOUT, DEFAULT_SAMPLE_FORMAT);
		Log.d(TAG, "run() called mMinBufferSize=" + mMinBufferSize);

		if (AudioRecord.ERROR_BAD_VALUE == mMinBufferSize) {
			mRecorderCallback.onError("parameters are not supported by the hardware.");
			return;
		}

		mAudioRecord = new AudioRecord(android.media.MediaRecorder.AudioSource.MIC, DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_LAYOUT, DEFAULT_SAMPLE_FORMAT, mMinBufferSize);
		try {
			mAudioRecord.startRecording();
		} catch (IllegalStateException e) {
			mRecorderCallback.onError(e.getMessage() + " [startRecording failed]");
			return;
		}

		byte[] sampleBuffer = new byte[READ_BUFFER_SIZE];
		try {
			while (!Thread.currentThread().isInterrupted()) {

				int result = mAudioRecord.read(sampleBuffer, 0, READ_BUFFER_SIZE);
				if (result > 0) {
					int frames = result / BYTES_PER_FRAME;
					long captureTimeNs = getCaptureTimeNs(frames);
					mFramesRead += frames;
					mRecorderCallback.onAudioData(sampleBuffer, result, captureTimeNs);
				}
			}
		} catch (Exception e) {
			mRecorderCallback.onError(e.getMessage());
		}

		mAudioRecord.release();
		mAudioRecord = null;
	}

	/**
	 * 计算刚读出的这一块中第一个采样的采集时刻（SystemClock.elapsedRealtimeNanos() 时基）。
	 *
	 * 不能用"读到数据的时间"：read 返回时这块数据已经在缓冲区里待了一段时间（至少一个块长），
	 * 而且读线程被调度晚了还会更久，直接用会让音频整体偏晚、并带有抖动。
	 *
	 * Android 7.0+ 用 AudioRecord.getTimestamp：它给出某一帧（framePosition）在声卡上被采集的时刻，
	 * 按帧差和采样率推算出本块第一个采样的采集时刻，精度在毫秒以内。
	 * 更早的系统没有这个接口：本块最后一个采样约在"刚刚"采到，往前推一个块长；
	 * 读线程调度带来的正向抖动由 Native 层的纠偏窗口过滤。
	 */
	private long getCaptureTimeNs(int chunkFrames) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
			if (mAudioTimestamp == null) mAudioTimestamp = new AudioTimestamp();
			if (mAudioRecord.getTimestamp(mAudioTimestamp, AudioTimestamp.TIMEBASE_BOOTTIME) == AudioRecord.SUCCESS) {
				return mAudioTimestamp.nanoTime
						+ (mFramesRead - mAudioTimestamp.framePosition) * NANOS_PER_SECOND / DEFAULT_SAMPLE_RATE;
			}
		}
		return SystemClock.elapsedRealtimeNanos() - chunkFrames * NANOS_PER_SECOND / DEFAULT_SAMPLE_RATE;
	}

	public interface AudioRecorderCallback {
		/**
		 * @param captureTimeNs 第一个采样的采集时刻，SystemClock.elapsedRealtimeNanos() 时基
		 */
		void onAudioData(byte[] data, int dataSize, long captureTimeNs);
		void onError(String msg);
	}
}
