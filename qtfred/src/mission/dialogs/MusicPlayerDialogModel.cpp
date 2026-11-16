#include "mission/dialogs/MusicPlayerDialogModel.h"

namespace fso::fred::dialogs {

MusicPlayerDialogModel::MusicPlayerDialogModel(QObject* parent, EditorViewport* viewport, MusicPlayer* player)
	: AbstractDialogModel(parent, viewport), _player(player)
{
	// the track list waits for the first dialog: editor.tbl's ignored files aren't known at startup
	_player->ensureTracksLoaded();

	// opens with the loaded track selected, or with nothing selected
	_player->setCurrentRow(_player->loadedRow());

	connect(_player, &MusicPlayer::stateChanged, this, &MusicPlayerDialogModel::modelChanged);
}

bool MusicPlayerDialogModel::apply()
{
	return true;
}

void MusicPlayerDialogModel::reject()
{
	if (!_viewport->Show_music_toolbar_controls)
		_player->stop();
}

} // namespace fso::fred::dialogs
