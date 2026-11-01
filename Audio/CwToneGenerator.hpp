#ifndef CW_TONE_GENERATOR_HPP__
#define CW_TONE_GENERATOR_HPP__

#include <QIODevice>
#include <QByteArray>
#include <QAudioFormat>
#include <QString>

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
};

#endif
