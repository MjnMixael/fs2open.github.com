#include <ui/dialogs/TableViewerDialog.h>
#include "MusicPlayerDialog.h"
#include "ui/util/SignalBlockers.h"
#include "ui_MusicPlayerDialog.h"
#include "ui/Theme.h"
#include "mission/EditorViewport.h"

namespace fso::fred::dialogs {

MusicPlayerDialog::MusicPlayerDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent), _viewport(viewport), ui(new Ui::MusicPlayerDialog()), _player(parent->musicPlayer())
{
	setFocus();
	ui->setupUi(this);

	fso::fred::bindStandardIcon(ui->prevButton, QStyle::SP_MediaSkipBackward);
	fso::fred::bindStandardIcon(ui->playButton, QStyle::SP_MediaPlay);
	fso::fred::bindStandardIcon(ui->nextButton, QStyle::SP_MediaSkipForward);
	fso::fred::bindStandardIcon(ui->stopButton, QStyle::SP_MediaStop);

	// opens with the loaded track selected, or with nothing selected
	_player->setCurrentRow(_player->loadedRow());

	populateList();
	updateUi();

	connect(_player, &MusicPlayer::stateChanged, this, &MusicPlayerDialog::updateUi);

	// without the toolbar controls, closing the dialog would leave no way to stop the music
	connect(this, &QDialog::finished, this, [this]() {
		if (!_viewport->Show_music_toolbar_controls)
			_player->stop();
	});
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
	for (const auto& track : _player->tracks()) {
		items.append(QString::fromStdString(track));
	}
	ui->musicList->addItems(items);
}

void MusicPlayerDialog::updateUi()
{
	util::SignalBlockers block(this);

	const int row = _player->currentRow();
	if (row < 0)
		ui->musicList->clearSelection();
	if (ui->musicList->currentRow() != row)
		ui->musicList->setCurrentRow(row);

	// Play turns into Pause while music plays
	const bool playing = _player->isPlaying();
	setBoundStandardIcon(ui->playButton, playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay);
	ui->playButton->setEnabled(playing || _player->currentRow() >= 0);
	ui->stopButton->setEnabled(_player->hasStream());
	ui->nextButton->setEnabled(_player->canSelectNext());
	ui->prevButton->setEnabled(_player->canSelectPrev());
	ui->autoplayCheck->setChecked(_player->autoplay());
}

void MusicPlayerDialog::on_musicList_itemSelectionChanged()
{
	int row = -1;
	const auto selected = ui->musicList->selectedItems();
	if (!selected.isEmpty())
		row = ui->musicList->row(selected.front());
	_player->setCurrentRow(row);
}

void MusicPlayerDialog::on_musicList_itemDoubleClicked(QListWidgetItem* item)
{
	// plays the track even while another one is playing, which the Play / Pause button can't
	_player->setCurrentRow(ui->musicList->row(item));
	_player->play();
}

void MusicPlayerDialog::on_playButton_clicked()
{
	_player->togglePlay();
}

void MusicPlayerDialog::on_stopButton_clicked()
{
	_player->stop();
}

void MusicPlayerDialog::on_nextButton_clicked()
{
	_player->selectNext();
}

void MusicPlayerDialog::on_prevButton_clicked()
{
	_player->selectPrev();
}

void MusicPlayerDialog::on_autoplayCheck_toggled(bool on)
{
	_player->setAutoplay(on);
}

void MusicPlayerDialog::on_musicTblButton_clicked()
{
	auto dialog = new TableViewerDialog(this, _viewport, "Music TBL Data", "music.tbl", "*-mus.tbm");
	dialog->show();
}

} // namespace fso::fred::dialogs
