#include "CwToneGenerator.hpp"

#include <QHash>
#include <QtMath>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <climits>

#include "moc_CwToneGenerator.cpp"

namespace
{
  // ITU/PARIS Morse table. Keyed on upper-case ASCII. Characters not in the
  // table are silently skipped during generation.
  QHash<QChar, QString> const& morseTable ()
  {
    static QHash<QChar, QString> const t {
      {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
      {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
      {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
      {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
      {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
      {'Z', "--.."},
      {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},{'4', "....-"},
      {'5', "....."},{'6', "-...."},{'7', "--..."},{'8', "---.."},{'9', "----."},
      {'.', ".-.-.-"},{',',"--..--"},{'?',"..--.."},{'/',"-..-."},{'=',"-...-"},
      {'+', ".-.-."},{'-',"-....-"},{'(',"-.--."},{')',"-.--.-"},{'@',".--.-."},
      {':', "---..."},{';',"-.-.-."},{'\'',".----."},{'"',".-..-."}
    };
    return t;
  }

  // Fill the integer sample buffer with a keyed sine burst of `durMs` ms
  // followed by `gapMs` ms of silence. Applies raised-cosine ramp of
  // rampMs at both edges to suppress clicks. 16-bit mono PCM.
  void appendBurst (QByteArray & out, int sampleRate, double toneHz,
                    int durMs, int gapMs, int rampMs, double amplitude)
  {
    int const nTone = (static_cast<qint64> (sampleRate) * durMs) / 1000;
    int const nGap  = (static_cast<qint64> (sampleRate) * gapMs)  / 1000;
    int const nRamp = (static_cast<qint64> (sampleRate) * rampMs) / 1000;
    out.reserve (out.size () + (nTone + nGap) * 2);
    double const w = 2.0 * M_PI * toneHz / static_cast<double> (sampleRate);
    for (int i = 0; i < nTone; ++i) {
      double env = 1.0;
      if (nRamp > 0) {
        if (i < nRamp) {
          env = 0.5 - 0.5 * std::cos (M_PI * i / nRamp);
        } else if (i >= nTone - nRamp) {
          int j = nTone - 1 - i;
          env = 0.5 - 0.5 * std::cos (M_PI * j / nRamp);
        }
      }
      double v = amplitude * env * std::sin (w * i);
      qint16 s = static_cast<qint16> (std::max (-32768.0, std::min (32767.0, v * 32767.0)));
      out.append (static_cast<char> (s & 0xff));
      out.append (static_cast<char> ((s >> 8) & 0xff));
    }
    // Trailing silence (inter-element gap)
    for (int i = 0; i < nGap; ++i) {
      out.append (char {0});
      out.append (char {0});
    }
  }

  void appendSilence (QByteArray & out, int sampleRate, int ms)
  {
    int const n = (static_cast<qint64> (sampleRate) * ms) / 1000;
    out.reserve (out.size () + n * 2);
    for (int i = 0; i < n; ++i) {
      out.append (char {0});
      out.append (char {0});
    }
  }
}

CwToneGenerator::CwToneGenerator (QObject * parent)
  : QIODevice {parent}
{
}

bool CwToneGenerator::generate (QString const& text, int wpm, int toneHz,
                                QAudioFormat const& fmt)
{
  samples_.clear ();
  pos_ = 0;
  total_micros_ = 0;
  continuous_ = false;
  if (text.isEmpty ()) return false;
  if (fmt.sampleSize () != 16) return false;
  if (fmt.sampleType () != QAudioFormat::SignedInt) return false;
  if (wpm < 5) wpm = 5;
  if (wpm > 50) wpm = 50;
  if (toneHz < 300) toneHz = 300;
  if (toneHz > 2000) toneHz = 2000;

  int const sampleRate = fmt.sampleRate ();
  int const unitMs = 1200 / wpm;       // PARIS: dit = 1200/WPM ms
  int const rampMs = 5;                // click suppression
  double const amplitude = 0.5;         // -6 dBFS

  auto const& table = morseTable ();
  QString upper = text.toUpper ();
  // Lead-in silence so the rig has time to settle after PTT
  appendSilence (samples_, sampleRate, 100);

  for (int i = 0; i < upper.size (); ++i) {
    QChar c = upper.at (i);
    if (c == QChar (' ')) {
      // Word space: 7 units total between chars; we already appended 3 units
      // (1 unit gap after last element + 2 units for inter-char). Add 4 more.
      appendSilence (samples_, sampleRate, 4 * unitMs);
      continue;
    }
    auto it = table.find (c);
    if (it == table.end ()) continue;
    QString const& code = it.value ();
    for (int k = 0; k < code.size (); ++k) {
      bool isDah = (code.at (k) == QChar ('-'));
      int durMs = isDah ? 3 * unitMs : unitMs;
      int gapMs = unitMs;              // intra-char gap
      appendBurst (samples_, sampleRate, toneHz, durMs, gapMs, rampMs, amplitude);
    }
    // After last element we added 1 unit gap. Add 2 more for 3-unit inter-char.
    appendSilence (samples_, sampleRate, 2 * unitMs);
  }
  // Tail silence (let rig keyed carrier decay before PTT off)
  appendSilence (samples_, sampleRate, 100);

  total_micros_ = (static_cast<qint64> (samples_.size () / 2) * 1000000) / sampleRate;

  open (QIODevice::ReadOnly);
  return !samples_.isEmpty ();
}

bool CwToneGenerator::generateContinuousTone (int toneHz, QAudioFormat const& fmt)
{
  samples_.clear ();
  pos_ = 0;
  total_micros_ = 0;
  continuous_ = true;
  if (fmt.sampleSize () != 16) return false;
  if (fmt.sampleType () != QAudioFormat::SignedInt) return false;
  if (toneHz < 300) toneHz = 300;
  if (toneHz > 2000) toneHz = 2000;

  int const sampleRate = fmt.sampleRate ();
  double const amplitude = 0.5;             // -6 dBFS
  // Find smallest buffer that is an integer number of tone periods so the
  // read-wraps are click-free. For 48 kHz / 700 Hz, 7 periods = 480
  // samples = 10 ms.
  int numPeriods = 1;
  while ((static_cast<qint64> (numPeriods) * sampleRate) % toneHz != 0 && numPeriods < toneHz) {
    ++numPeriods;
  }
  int const nSamples = (numPeriods * sampleRate) / toneHz;
  double const w = 2.0 * M_PI * toneHz / static_cast<double> (sampleRate);
  int const nRamp = (sampleRate * 5) / 1000;  // 5 ms fade-in on very first play
  samples_.reserve (nSamples * 2);
  for (int i = 0; i < nSamples; ++i) {
    double env = 1.0;
    if (i < nRamp) {
      env = 0.5 - 0.5 * std::cos (M_PI * i / nRamp);
    }
    double v = amplitude * env * std::sin (w * i);
    qint16 s = static_cast<qint16> (std::max (-32768.0, std::min (32767.0, v * 32767.0)));
    samples_.append (static_cast<char> (s & 0xff));
    samples_.append (static_cast<char> ((s >> 8) & 0xff));
  }
  open (QIODevice::ReadOnly);
  return !samples_.isEmpty ();
}

qint64 CwToneGenerator::bytesAvailable () const
{
  if (continuous_) {
    return INT_MAX;  // never ends until explicitly stopped
  }
  if (live_) {
    QMutexLocker lk {&live_mutex_};
    if (live_ended_ && live_samples_.isEmpty ()) return QIODevice::bytesAvailable ();
    return INT_MAX;
  }
  return samples_.size () - pos_ + QIODevice::bytesAvailable ();
}

bool CwToneGenerator::atEnd () const
{
  if (continuous_) return false;
  if (live_) {
    QMutexLocker lk {&live_mutex_};
    return live_ended_ && live_samples_.isEmpty ();
  }
  return pos_ >= samples_.size ();
}

bool CwToneGenerator::startLiveStream (int wpm, int toneHz, QAudioFormat const& fmt)
{
  samples_.clear ();
  pos_ = 0;
  total_micros_ = 0;
  continuous_ = false;
  if (fmt.sampleSize () != 16) return false;
  if (fmt.sampleType () != QAudioFormat::SignedInt) return false;
  if (wpm < 5)  wpm = 5;
  if (wpm > 50) wpm = 50;
  if (toneHz < 300) toneHz = 300;
  if (toneHz > 2000) toneHz = 2000;
  QMutexLocker lk {&live_mutex_};
  live_ = true;
  live_ended_ = false;
  live_samples_.clear ();
  live_sample_rate_ = fmt.sampleRate ();
  live_wpm_ = wpm;
  live_tone_hz_ = toneHz;
  // Lead-in silence so the rig has time to settle after PTT
  int const n = (live_sample_rate_ * 100) / 1000;
  live_samples_.reserve (n * 2 + 1024);
  for (int i = 0; i < n; ++i) {
    live_samples_.append (char {0});
    live_samples_.append (char {0});
  }
  open (QIODevice::ReadOnly);
  return true;
}

void CwToneGenerator::appendTextLive (QString const& text)
{
  QMutexLocker lk {&live_mutex_};
  if (!live_ || live_ended_ || text.isEmpty ()) return;
  int const unitMs = 1200 / live_wpm_;
  int const rampMs = 5;
  double const amplitude = 0.5;
  auto const& table = morseTable ();
  QString upper = text.toUpper ();
  for (int i = 0; i < upper.size (); ++i) {
    QChar c = upper.at (i);
    if (c == QChar (' ')) {
      // Word gap: 4 extra units on top of the 3 already appended after
      // previous character (or just 7 if first char — but we never
      // start with a space in append, user types normally).
      int const n = (live_sample_rate_ * 4 * unitMs) / 1000;
      for (int k = 0; k < n; ++k) { live_samples_.append (char {0}); live_samples_.append (char {0}); }
      continue;
    }
    auto it = table.find (c);
    if (it == table.end ()) continue;
    QString const& code = it.value ();
    for (int k = 0; k < code.size (); ++k) {
      bool isDah = (code.at (k) == QChar ('-'));
      int durMs = isDah ? 3 * unitMs : unitMs;
      int gapMs = unitMs;
      appendBurst (live_samples_, live_sample_rate_, live_tone_hz_,
                   durMs, gapMs, rampMs, amplitude);
    }
    // 2 extra units for 3-unit inter-char
    int const n = (live_sample_rate_ * 2 * unitMs) / 1000;
    for (int k = 0; k < n; ++k) { live_samples_.append (char {0}); live_samples_.append (char {0}); }
  }
}

void CwToneGenerator::endLiveStream ()
{
  QMutexLocker lk {&live_mutex_};
  if (!live_) return;
  live_ended_ = true;
  // Tail silence so last element can decay before PTT off
  int const n = (live_sample_rate_ * 100) / 1000;
  for (int k = 0; k < n; ++k) { live_samples_.append (char {0}); live_samples_.append (char {0}); }
}

qint64 CwToneGenerator::readData (char * data, qint64 maxSize)
{
  if (maxSize <= 0) return 0;
  if (live_) {
    QMutexLocker lk {&live_mutex_};
    qint64 available = live_samples_.size ();
    if (available == 0) {
      if (live_ended_) return 0;  // end of stream
      // Keep stream alive: return silence (16-bit mono)
      qint64 toCopy = std::min<qint64> (maxSize, 1920);  // 20 ms of silence @ 48 kHz
      // round to even bytes (16-bit samples)
      toCopy &= ~qint64 (1);
      if (toCopy <= 0) return 0;
      memset (data, 0, static_cast<size_t> (toCopy));
      return toCopy;
    }
    qint64 toCopy = std::min (maxSize, available);
    memcpy (data, live_samples_.constData (), static_cast<size_t> (toCopy));
    live_samples_.remove (0, static_cast<int> (toCopy));
    return toCopy;
  }
  if (samples_.isEmpty ()) return 0;
  if (!continuous_) {
    qint64 remaining = samples_.size () - pos_;
    if (remaining <= 0) return 0;
    qint64 toCopy = std::min (maxSize, remaining);
    memcpy (data, samples_.constData () + pos_, static_cast<size_t> (toCopy));
    pos_ += toCopy;
    return toCopy;
  }
  // Continuous mode: wrap pos_ around the buffer so the stream never ends.
  qint64 written = 0;
  while (written < maxSize) {
    qint64 remaining = samples_.size () - pos_;
    if (remaining <= 0) {
      pos_ = 0;
      remaining = samples_.size ();
    }
    qint64 toCopy = std::min (maxSize - written, remaining);
    memcpy (data + written, samples_.constData () + pos_, static_cast<size_t> (toCopy));
    pos_ += toCopy;
    written += toCopy;
  }
  return written;
}
