#include "OpenSLAudioOutput.h"
#include <cstring>
#include <LogUtil.h>
#include <core/Clock.h>

bool OpenSLAudioOutput::Open(int sampleRate, int channels, Source *source) {
    if (channels != 1 && channels != 2) {
        LOGCATE("OpenSLAudioOutput::Open unsupported channels %d", channels);
        return false;
    }
    m_Source = source;
    m_Closing = false;
    m_QueuedDataChunks = 0;
    m_FillIndex = 0;
    m_BufferBytes = FRAMES_PER_BUFFER * channels * 2;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        m_Buffers[i].assign(static_cast<size_t>(m_BufferBytes), 0);
        m_ChunkInfos[i] = ChunkInfo();
    }

    SLresult result;
    do {
        result = slCreateEngine(&m_EngineObj, 0, nullptr, 0, nullptr, nullptr);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_EngineObj)->Realize(m_EngineObj, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_EngineObj)->GetInterface(m_EngineObj, SL_IID_ENGINE, &m_Engine);
        if (result != SL_RESULT_SUCCESS) break;

        result = (*m_Engine)->CreateOutputMix(m_Engine, &m_OutputMixObj, 0, nullptr, nullptr);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_OutputMixObj)->Realize(m_OutputMixObj, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) break;

        SLDataLocator_AndroidSimpleBufferQueue locatorQueue = {
                SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, BUFFER_COUNT};
        SLDataFormat_PCM pcm = {
                SL_DATAFORMAT_PCM,
                static_cast<SLuint32>(channels),
                static_cast<SLuint32>(sampleRate) * 1000,   // OpenSL 的采样率单位是毫赫兹
                SL_PCMSAMPLEFORMAT_FIXED_16,
                SL_PCMSAMPLEFORMAT_FIXED_16,
                channels == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER,
                SL_BYTEORDER_LITTLEENDIAN
        };
        SLDataSource dataSource = {&locatorQueue, &pcm};
        SLDataLocator_OutputMix locatorMix = {SL_DATALOCATOR_OUTPUTMIX, m_OutputMixObj};
        SLDataSink dataSink = {&locatorMix, nullptr};

        const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
        const SLboolean req[1] = {SL_BOOLEAN_TRUE};
        result = (*m_Engine)->CreateAudioPlayer(m_Engine, &m_PlayerObj, &dataSource, &dataSink, 1, ids, req);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_PlayerObj)->Realize(m_PlayerObj, SL_BOOLEAN_FALSE);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_PLAY, &m_Play);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_PlayerObj)->GetInterface(m_PlayerObj, SL_IID_BUFFERQUEUE, &m_BufferQueue);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_BufferQueue)->RegisterCallback(m_BufferQueue, BufferQueueCallback, this);
        if (result != SL_RESULT_SUCCESS) break;
        result = (*m_Play)->SetPlayState(m_Play, SL_PLAYSTATE_PAUSED);
        if (result != SL_RESULT_SUCCESS) break;

        // 先放满静音：回调链由 "播完一块" 驱动，必须预先有块在队列里
        for (int i = 0; i < BUFFER_COUNT && result == SL_RESULT_SUCCESS; ++i) {
            result = (*m_BufferQueue)->Enqueue(m_BufferQueue, m_Buffers[i].data(),
                                               static_cast<SLuint32>(m_BufferBytes));
        }
    } while (false);

    if (result != SL_RESULT_SUCCESS) {
        LOGCATE("OpenSLAudioOutput::Open fail, result=%d", static_cast<int>(result));
        Close();
        return false;
    }
    LOGCATI("OpenSLAudioOutput::Open %dHz %dch, %d x %d bytes", sampleRate, channels, BUFFER_COUNT, m_BufferBytes);
    return true;
}

void OpenSLAudioOutput::Start() {
    if (m_Play) (*m_Play)->SetPlayState(m_Play, SL_PLAYSTATE_PLAYING);
}

void OpenSLAudioOutput::Pause() {
    if (m_Play) (*m_Play)->SetPlayState(m_Play, SL_PLAYSTATE_PAUSED);
}

void OpenSLAudioOutput::Close() {
    m_Closing = true;
    if (m_Play) (*m_Play)->SetPlayState(m_Play, SL_PLAYSTATE_STOPPED);
    // Destroy 会等待正在执行的回调返回，之后不会再有回调
    if (m_PlayerObj) {
        (*m_PlayerObj)->Destroy(m_PlayerObj);
        m_PlayerObj = nullptr;
    }
    m_Play = nullptr;
    m_BufferQueue = nullptr;
    if (m_OutputMixObj) {
        (*m_OutputMixObj)->Destroy(m_OutputMixObj);
        m_OutputMixObj = nullptr;
    }
    if (m_EngineObj) {
        (*m_EngineObj)->Destroy(m_EngineObj);
        m_EngineObj = nullptr;
    }
    m_Engine = nullptr;
    m_QueuedDataChunks = 0;
}

void OpenSLAudioOutput::BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context) {
    static_cast<OpenSLAudioOutput *>(context)->OnBufferDone();
}

void OpenSLAudioOutput::OnBufferDone() {
    if (m_Closing || m_BufferQueue == nullptr) return;

    const int finished = m_FillIndex;
    if (m_ChunkInfos[finished].hasData) m_QueuedDataChunks--;

    // 最早入队的块播完了，紧随其后的那块此刻开始播放
    const int playing = (finished + 1) % BUFFER_COUNT;
    m_Source->OnChunkStarted(m_ChunkInfos[playing], m_Buffers[playing].data(), m_BufferBytes, Clock::Now());

    ChunkInfo info;
    m_Source->OnFillBuffer(m_Buffers[finished].data(), m_BufferBytes, &info);
    m_ChunkInfos[finished] = info;
    if (info.hasData) m_QueuedDataChunks++;

    SLresult result = (*m_BufferQueue)->Enqueue(m_BufferQueue, m_Buffers[finished].data(),
                                                static_cast<SLuint32>(m_BufferBytes));
    if (result != SL_RESULT_SUCCESS) {
        // 入队失败回调链就断了；只记一次日志，避免刷屏
        if (!m_EnqueueErrorLogged) {
            LOGCATE("OpenSLAudioOutput::OnBufferDone Enqueue fail, result=%d", static_cast<int>(result));
            m_EnqueueErrorLogged = true;
        }
        if (info.hasData) m_QueuedDataChunks--;
        m_ChunkInfos[finished] = ChunkInfo();
    }
    m_FillIndex = playing;
}
