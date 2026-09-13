#include "ExportCabrillo.h"

#include <QApplication>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QStandardPaths>

#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "MessageBox.hpp"
#include "models/CabrilloLog.hpp"
#include "models/Bands.hpp"

#include "ui_ExportCabrillo.h"
#include "moc_ExportCabrillo.cpp"

namespace
{
  QString extractField (QString const& record, QString const& fieldName)
  {
    int fieldNameIndex = record.indexOf ('<' + fieldName + ':', 0, Qt::CaseInsensitive);
    if (fieldNameIndex >= 0)
      {
        int closingBracketIndex = record.indexOf ('>', fieldNameIndex);
        int fieldLengthIndex = record.indexOf (':', fieldNameIndex);
        int dataTypeIndex = -1;
        if (fieldLengthIndex >= 0)
          {
            dataTypeIndex = record.indexOf (':', fieldLengthIndex + 1);
            if (dataTypeIndex > closingBracketIndex)
              dataTypeIndex = -1;
          }
        else
          {
            return QString {};
          }

        if (closingBracketIndex > fieldNameIndex && fieldLengthIndex > fieldNameIndex && fieldLengthIndex < closingBracketIndex)
          {
            int fieldLengthCharCount = closingBracketIndex - fieldLengthIndex - 1;
            if (dataTypeIndex >= 0)
              fieldLengthCharCount -= 2;
            QString fieldLengthString = record.mid (fieldLengthIndex + 1, fieldLengthCharCount);
            int fieldLength = fieldLengthString.toInt ();
            if (fieldLength > 0)
              {
                return record.mid (closingBracketIndex + 1, fieldLength);
              }
          }
      }
    return QString {};
  }
}

ExportCabrillo::ExportCabrillo (QSettings * settings, Configuration const * configuration
                                , CabrilloLog const * log, QWidget * parent)
  : QDialog {parent},
    settings_ {settings},
    configuration_ {configuration},
    log_ {log},
    ui {new Ui::ExportCabrillo}
{
  ui->setupUi (this);
  read_settings ();
  setWindowTitle (QApplication::applicationName() + " - Export Cabrillo");
  populate_log_sources ();
  connect (ui->buttonBox, &QDialogButtonBox::accepted, this, &ExportCabrillo::save_log);
}

ExportCabrillo::~ExportCabrillo ()
{
  write_settings ();
}

void ExportCabrillo::read_settings ()
{
  SettingsGroup group {settings_, "ExportCabrillo"};
  restoreGeometry (settings_->value("window/geometry").toByteArray());
  ui->location_line_edit->setText(settings_->value("Location").toString());
  ui->contest_line_edit->setText(settings_->value("Contest").toString());
  ui->call_line_edit->setText(settings_->value("Callsign").toString());
  ui->category_op_line_edit->setText(settings_->value("Category-Operator").toString());
  ui->category_xmtr_line_edit->setText(settings_->value("Category-Transmitter").toString());
  ui->category_pwr_line_edit->setText(settings_->value("Category-Power").toString());
  ui->category_assisted_line_edit->setText(settings_->value("Category-Assisted").toString());
  ui->category_band_line_edit->setText(settings_->value("Category-Band").toString());
  ui->claimed_line_edit->setText(settings_->value("Claimed-Score").toString());
  ui->operators_line_edit->setText(settings_->value("Operators").toString());
  ui->club_line_edit->setText(settings_->value("Club").toString());
  ui->name_line_edit->setText(settings_->value("Name").toString());
  ui->addr_1_line_edit->setText(settings_->value("Address1").toString());
  ui->addr_2_line_edit->setText(settings_->value("Address2").toString());
}

void ExportCabrillo::write_settings ()
{
  SettingsGroup group {settings_, "ExportCabrillo"};
  settings_->setValue ("window/geometry", saveGeometry ());
  settings_->setValue("Location",ui->location_line_edit->text());
  settings_->setValue("Contest",ui->contest_line_edit->text());
  settings_->setValue("Callsign",ui->call_line_edit->text());
  settings_->setValue("Category-Operator",ui->category_op_line_edit->text());
  settings_->setValue("Category-Transmitter",ui->category_xmtr_line_edit->text());
  settings_->setValue("Category-Power",ui->category_pwr_line_edit->text());
  settings_->setValue("Category-Assisted",ui->category_assisted_line_edit->text());
  settings_->setValue("Category-Band",ui->category_band_line_edit->text());
  settings_->setValue("Claimed-Score",ui->claimed_line_edit->text());
  settings_->setValue("Operators",ui->operators_line_edit->text());
  settings_->setValue("Club",ui->club_line_edit->text());
  settings_->setValue("Name",ui->name_line_edit->text());
  settings_->setValue("Address1",ui->addr_1_line_edit->text());
  settings_->setValue("Address2",ui->addr_2_line_edit->text());
}

void ExportCabrillo::populate_log_sources ()
{
  ui->log_source_combo->addItem (tr ("(Contest log)"));

  QDir data_dir {configuration_->writeable_data_dir ()};
  auto adi_files = data_dir.entryList (QStringList {"wsjtx_log*.adi"}, QDir::Files, QDir::Name);
  for (auto const& f : adi_files)
    {
      ui->log_source_combo->addItem (f);
    }

  // pre-select the file matching the current callsign
  auto call = configuration_->my_callsign ().trimmed ();
  QString current_log;
  if (call.isEmpty ())
    {
      current_log = "wsjtx_log.adi";
    }
  else
    {
      current_log = "wsjtx_log_" + QString {call}.replace ('/', '-') + ".adi";
    }
  int idx = ui->log_source_combo->findText (current_log);
  if (idx >= 0)
    {
      ui->log_source_combo->setCurrentIndex (idx);
    }
}

QList<QsoRecord> ExportCabrillo::parse_adi_file (QString const& path)
{
  QList<QsoRecord> qsos;
  QFile inputFile {path};
  if (!inputFile.open (QFile::ReadOnly | QFile::Text))
    {
      return qsos;
    }

  QTextStream in {&inputFile};
  QString buffer;
  bool pre_read {false};
  int end_position {-1};

  // skip optional header
  do
    {
      buffer += in.readLine () + '\n';
      if (buffer.startsWith (QChar {'<'}))
        {
          pre_read = true;
        }
      else
        {
          end_position = buffer.indexOf ("<EOH>", 0, Qt::CaseInsensitive);
        }
    }
  while (!in.atEnd () && !pre_read && end_position < 0);

  if (!pre_read)
    {
      if (end_position < 0)
        {
          return qsos;
        }
      buffer.remove (0, end_position + 5);
    }

  while (!in.atEnd () || buffer.contains ('<'))
    {
      end_position = buffer.indexOf ("<EOR>", 0, Qt::CaseInsensitive);
      while (end_position < 0 && !in.atEnd ())
        {
          buffer += in.readLine () + '\n';
          end_position = buffer.indexOf ("<EOR>", 0, Qt::CaseInsensitive);
        }

      if (end_position >= 0)
        {
          auto record = buffer.left (end_position + 5).trimmed ();
          auto next_record = buffer.indexOf (QChar {'<'}, end_position + 5);
          buffer.remove (0, next_record >= 0 ? next_record : buffer.size ());
          record = record.mid (record.indexOf (QChar {'<'}));

          auto call = extractField (record, "CALL");
          if (call.isEmpty ()) continue;

          QsoRecord qso;
          qso.call = call;

          auto mode = extractField (record, "MODE").toUpper ();
          if (mode.isEmpty () || "MFSK" == mode)
            {
              mode = extractField (record, "SUBMODE").toUpper ();
            }
          qso.mode = mode;

          // frequency: FREQ field is in MHz
          auto freq_str = extractField (record, "FREQ");
          if (!freq_str.isEmpty ())
            {
              qso.freq = static_cast<Radio::Frequency> (freq_str.toDouble () * 1e6 + 0.5);
            }
          else
            {
              qso.freq = 0;
            }

          // date/time
          auto date_str = extractField (record, "QSO_DATE");
          auto time_str = extractField (record, "TIME_ON");
          if (!date_str.isEmpty ())
            {
              auto date = QDate::fromString (date_str, "yyyyMMdd");
              QTime time;
              if (!time_str.isEmpty ())
                {
                  if (time_str.size () >= 6)
                    time = QTime::fromString (time_str.left (6), "hhmmss");
                  else
                    time = QTime::fromString (time_str.left (4), "hhmm");
                }
              qso.when = QDateTime {date, time, Qt::UTC};
            }

          // exchange: auto-detect (contest fields take priority)
          auto stx = extractField (record, "STX_STRING");
          if (stx.isEmpty ()) stx = extractField (record, "STX");
          if (stx.isEmpty ()) stx = extractField (record, "RST_SENT");
          qso.exchange_sent = stx;

          auto srx = extractField (record, "SRX_STRING");
          if (srx.isEmpty ()) srx = extractField (record, "SRX");
          if (srx.isEmpty ()) srx = extractField (record, "RST_RCVD");
          qso.exchange_rcvd = srx;

          qsos.append (qso);
        }
      else
        {
          break;
        }
    }

  // sort by datetime
  std::sort (qsos.begin (), qsos.end (), [] (QsoRecord const& a, QsoRecord const& b) {
    return a.when < b.when;
  });

  return qsos;
}

QString ExportCabrillo::cabrillo_frequency_string (Radio::Frequency frequency) const
{
  QString result;
  auto band = configuration_->bands ()->find (frequency);
  if ("1mm" == band) result = "LIGHT";
  else if ("2mm" == band) result = "241G";
  else if ("2.5mm" == band) result = "134G";
  else if ("4mm" == band) result = "75G";
  else if ("6mm" == band) result = "47G";
  else if ("1.25cm" == band) result = "24G";
  else if ("3cm" == band) result = "10G";
  else if ("6cm" == band) result = "5.7G";
  else if ("9cm" == band) result = "3.4G";
  else if ("13cm" == band) result = "2.3G";
  else if ("23cm" == band) result = "1.2G";
  else if ("33cm" == band) result = "902";
  else if ("70cm" == band) result = "432";
  else if ("1.25m" == band) result = "222";
  else if ("2m" == band) result = "144";
  else if ("4m" == band) result = "70";
  else if ("6m" == band) result = "50";
  else result = QString::number (frequency / 1000ull);
  return result;
}

void ExportCabrillo::save_log ()
{
  auto fname = QFileDialog::getSaveFileName (this
                                             , tr ("Save Log File")
                                             , configuration_->writeable_data_dir ().absolutePath ()
                                             , tr ("Cabrillo Log (*.cbr)"));
  if (fname.size ())
    {
      QFile f {fname};
      if (f.open (QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out {&f};
        out << "START-OF-LOG:3.0\n"
            << "LOCATION: " << ui->location_line_edit->text() << '\n'
            << "CONTEST: " << ui->contest_line_edit->text() << '\n'
            << "CALLSIGN: " << ui->call_line_edit->text() << '\n'
            << "CATEGORY-OPERATOR: " << ui->category_op_line_edit->text() << '\n'
            << "CATEGORY-TRANSMITTER: " << ui->category_xmtr_line_edit->text() << '\n'
            << "CATEGORY-POWER: " << ui->category_pwr_line_edit->text() << '\n'
            << "CATEGORY-ASSISTED: " << ui->category_assisted_line_edit->text() << '\n'
            << "CATEGORY-BAND: " << ui->category_band_line_edit->text() << '\n'
            << "CLAIMED-SCORE: " << ui->claimed_line_edit->text() << '\n'
            << "OPERATORS: " << ui->operators_line_edit->text() << '\n'
            << "CLUB: " << ui->club_line_edit->text() << '\n'
            << "NAME: " << ui->name_line_edit->text() << '\n'
            << "ADDRESS: " << ui->addr_1_line_edit->text() << '\n'
            << "ADDRESS: " << ui->addr_2_line_edit->text() << '\n';

        if (ui->log_source_combo->currentIndex () == 0)
          {
            // contest log (original behavior)
            if (log_) log_->export_qsos (out);
          }
        else
          {
            // ADI file
            auto adi_path = configuration_->writeable_data_dir ().absoluteFilePath (ui->log_source_combo->currentText ());
            auto qsos = parse_adi_file (adi_path);
            auto my_call = ui->call_line_edit->text ();
            if (my_call.isEmpty ())
              {
                my_call = configuration_->my_callsign ();
              }
            for (auto const& qso : qsos)
              {
                out << QString {"QSO: %1 DG %2 %3 %4 %5 %6\n"}
                  .arg (cabrillo_frequency_string (qso.freq), 5)
                  .arg (qso.when.toString ("yyyy-MM-dd hhmm"))
                  .arg (my_call, -12)
                  .arg (qso.exchange_sent, -13)
                  .arg (qso.call, -12)
                  .arg (qso.exchange_rcvd, -13);
              }
          }

        out << "END-OF-LOG:"
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
            << Qt::endl
#else
            << endl
#endif
          ;
        return;
      } else {
        auto const& message = tr ("Cannot open \"%1\" for writing: %2")
          .arg (f.fileName ()).arg (f.errorString ());
        MessageBox::warning_message (this, tr ("Export Cabrillo File Error"), message);
      }
    }
  setResult (Rejected);
}
