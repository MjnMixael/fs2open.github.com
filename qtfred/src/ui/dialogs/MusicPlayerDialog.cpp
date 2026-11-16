#include <ui/dialogs/TableViewerDialog.h>
#include "MusicPlayerDialog.h"
#include "ui/util/SignalBlockers.h"
#include "ui_MusicPlayerDialog.h"
#include "ui/Theme.h"

namespace fso::fred::dialogs {

MusicPlayerDialog::MusicPlayerDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent), _viewport(viewport), ui(new Ui::MusicPlayerDialog()),
	  _model(new MusicPlayerDialogModel(this, viewport, parent->musicPlayer()))
{
	setFocus();
	ui->setupUi(this);

	fso::fred::bindStandardIcon(ui->prevButton, QStyle::SP_MediaSkipBackward);
	fso::fred::bindStandardIcon(ui->playButton, QStyle::SP_MediaPlay);
	fso::fred::bindStandardIcon(ui->nextButton, QStyle::SP_MediaSkipForward);
	fso::fred::bindStandardIcon(ui->stopButton, QStyle::SP_MediaStop);

	populateList();
	updateUi();

	connect(_model.get(), &AbstractDialogModel::modelChanged, this, &MusicPlayerDialog::updateUi);

	// closing the dialog any way (X, Escape) ends in finished
	connect(this, &QDialog::finished, _model.get(), &MusicPlayerDialogModel::reject);
}

MusicPlayerDialog::~MusicPlayerDialog()
{
	// the list clears its selection as it is destroyed; that must not reach the player
	ui->musicList->blockSignals(true);
}

void MusicPlayerDialog::accept()
{
	// not used
}

void MusicPlayerDialog::populateList()
{
	util::SignalBlockers block(this);
	ui->musicList->clear();
	QStringList items;
	for (const auto& track : _model->tracks()) {
		items.append(QString::fromStdString(track));
	}
	ui->musicList->addItems(items);
}

void MusicPlayerDialog::updateUi()
{
	util::SignalBlockers block(this);

	const int row = _model->currentRow();
	if (row < 0)
		ui->musicList->clearSelection();
	if (ui->musicList->currentRow() != row)
		ui->musicList->setCurrentRow(row);

	// Play turns into Pause while music plays
	const bool playing = _model->isPlaying();
	setBoundStandardIcon(ui->playButton, playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay);
	ui->playButton->setEnabled(playing || _model->currentRow() >= 0);
	ui->stopButton->setEnabled(_model->hasStream());
	ui->nextButton->setEnabled(_model->canSelectNext());
	ui->prevButton->setEnabled(_model->canSelectPrev());
	ui->autoplayCheck->setChecked(_model->autoplay());
}

void MusicPlayerDialog::on_musicList_itemSelectionChanged()
{
	int row = -1;
	const auto selected = ui->musicList->selectedItems();
	if (!selected.isEmpty())
		row = ui->musicList->row(selected.front());
	_model->setCurrentRow(row);
}

void MusicPlayerDialog::on_musicList_itemDoubleClicked(QListWidgetItem* item)
{
	// plays the track even while another one is playing, which the Play / Pause button can't
	_model->setCurrentRow(ui->musicList->row(item));
	_model->play();
}

void MusicPlayerDialog::on_playButton_clicked()
{
	_model->togglePlay();
}

void MusicPlayerDialog::on_stopButton_clicked()
{
	_model->stop();
}

void MusicPlayerDialog::on_nextButton_clicked()
{
	_model->selectNext();
}

void MusicPlayerDialog::on_prevButton_clicked()
{
	_model->selectPrev();
}

void MusicPlayerDialog::on_autoplayCheck_toggled(bool on)
{
	_model->setAutoplay(on);
}

void MusicPlayerDialog::on_musicTblButton_clicked()
{
	auto dialog = new TableViewerDialog(this, _viewport, "Music TBL Data", "music.tbl", "*-mus.tbm");
	dialog->show();
}

} // namespace fso::fred::dialogs
