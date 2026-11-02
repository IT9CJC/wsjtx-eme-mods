#ifndef CW_TONE_GENERATOR_HPP__
#define CW_TONE_GENERATOR_HPP__

#include <QIODevice>
#include <QByteArray>
#include <QAudioFormat>
#include <QString>
#include <QMutex>

// Generates PCM audio for CW keyed sidetone transmission via soundcard.
// Used when the rig does not support CAT keyer (rig_send_morse unavailable).
// Pre-generates the entire message into a sample buffer at construction time,
// then streams it via QIODevice::readData for QAudioOutput consumption.
class CwToneGenerator : public QIODevice
{
  Q_OBJECT
public:
  explicit CwToneGenerator (QObject * parent = nullptr);

  // Build the sample buffer for the given message. Returns false if format
  // is unsupported or message empty. Call before open()/QAudioOutput::start().
  bool generate (QString const& text, int wpm, int toneHz,
                 QAudioFormat const& fmt);

  // Build a short seamlessly-looping buffer of a continuous sine wave for
  // tune / antenna matching use. Buffer length is an integer number of
  // periods so wrap-around in readData() is click-free.
  bool generateContinuousTone (int toneHz, QAudioFormat const& fmt);

  // Live streaming mode: start an open-ended stream whose content is
  // appended on the fly via appendTextLive(). readData() returns silence
  // when the pending buffer is empty, keeping the audio pipeline alive
  // between keystrokes. Call endLiveStream() to drain and finish.
  bool startLiveStream (int wpm, int toneHz, QAudioFormat const& fmt);
  void appendTextLive (QString const& text);
  void endLiveStream ();

  qint64 totalMicros () const { return total_micros_; }

  bool isSequential () const override { return true; }
  qint64 bytesAvailable () const override;
  bool atEnd () const override;

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const *, qint64) override { return -1; }

private:
  QByteArray samples_;
  qint64 pos_ {0};
  qint64 total_micros_ {0};
  bool continuous_ {false};

  // Live stream state (protected by live_mutex_)
  bool live_ {false};
  bool live_ended_ {false};        // user asked to end, drain and finish
  QByteArray live_samples_;        // generated samples waiting to be read
  int live_sample_rate_ {48000};
  int live_wpm_ {25};
  int live_tone_hz_ {700};
  mutable QMutex live_mutex_;
};

#endif
