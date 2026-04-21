#include "GameStateExecutionContext.h"

#include "events/events.h"

namespace executor {

GameStateExecutionContext::GameStateExecutionContext(GS_STATE contextState, int stateInstance)
	: m_contextState(contextState), m_stateInstanceId(stateInstance), m_hasGameState(GameState_Stack_Valid())
{
}
GameStateExecutionContext::~GameStateExecutionContext() = default;

IExecutionContext::State GameStateExecutionContext::determineContextState() const
{
	if (!m_hasGameState) {
		// Captured in an environment that has no game-sequence stack (e.g. FRED). Keep running.
		return IExecutionContext::State::Valid;
	}

	const auto depth = gameseq_get_state_idx(m_contextState);

	if (depth < 0) {
		// Our game state is not present anymore
		return IExecutionContext::State::Invalid;
	}

	if (gameseq_get_state() == m_contextState) {
		// Check if this is still the same instance
		if (gameseq_get_state_instance_id() != m_stateInstanceId) {
			return IExecutionContext::State::Invalid;
		}

		// This is still the same state and it is active
		return State::Valid;
	}

	// We are not in the correct state but it is still in the stack so we might return to it at some point
	return State::Suspended;
}

std::shared_ptr<IExecutionContext> GameStateExecutionContext::captureContext()
{
	if (!GameState_Stack_Valid()) {
		return std::make_shared<GameStateExecutionContext>(GS_STATE_INVALID, -1);
	}

	return std::make_shared<GameStateExecutionContext>(static_cast<GS_STATE>(gameseq_get_state()),
		gameseq_get_state_instance_id());
}

} // namespace executor
