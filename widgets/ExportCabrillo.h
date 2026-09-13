// -*- Mode: C++ -*-
#ifndef EXPORTCABRILLO_H
#define EXPORTCABRILLO_H

#include <QDialog>
#include <QScopedPointer>
#include <QList>
#include <QDateTime>
#include <QString>

#include "Radio.hpp"

class QSettings;
class Configuration;
class CabrilloLog;
namespace Ui {
  class ExportCabrillo;
}

struct QsoRecord
{
  Radio::Frequency freq;
  QString mode;
  QDateTime when;
  QString call;
  QString exchange_sent;
  QString exchange_rcvd;
};

class ExportCabrillo final
  : public QDialog
{
  Q_OBJECT

public:
  explicit ExportCabrillo (QSettings *, Configuration const *
                           , CabrilloLog const *, QWidget * parent = nullptr);
  ~ExportCabrillo ();

private:
  void read_settings();
  void write_settings();
  void populate_log_sources ();
  QList<QsoRecord> parse_adi_file (QString const& path);
  QString cabrillo_frequency_string (Radio::Frequency frequency) const;
  void save_log ();

  QSettings * settings_;
  Configuration const * configuration_;
  CabrilloLog const * log_;
  QScopedPointer<Ui::ExportCabrillo> ui;
};

#endif
