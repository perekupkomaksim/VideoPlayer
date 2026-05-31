#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <memory>

#include "PacketQueue.h"
#include "Clock.h"
#include "VideoDecoder.h"
#include "AudioOutput.h"
#include "VideoRenderer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class MediaPlayer {
public:
    explicit MediaPlayer(const std::string& filePath);
    ~MediaPlayer();

    bool run();

private:
    void togglePlayPause();
    void seek(double deltaSec);
    void changeVolume(int delta);
    void stop();

    void readLoop();

    bool openFile();
    bool openVideoCodec();
    bool openAudioCodec();
    void cleanup();

    std::string m_filePath;

    AVFormatContext* m_fmt   {nullptr};
    int m_vidIdx{-1};
    int m_audIdx{-1};
    AVCodecContext* m_vidCtx{nullptr};
    AVCodecContext* m_audCtx{nullptr};

    PacketQueue m_videoQueue;
    PacketQueue m_audioQueue;
    Clock m_audioClock;

    std::unique_ptr<VideoDecoder> m_videoDecoder;
    std::unique_ptr<AudioOutput> m_audioOutput;
    std::unique_ptr<VideoRenderer> m_renderer;

    std::thread m_readThread;
    std::atomic<bool> m_stopRead{false};
    std::atomic<bool> m_seeking {false};

    std::atomic<bool> m_paused {false};
    std::atomic<int> m_volume {100};
    bool m_muted {false};
    int m_volumeBeforeMute{100};
};