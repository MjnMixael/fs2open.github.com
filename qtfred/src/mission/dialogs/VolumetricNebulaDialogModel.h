#pragma once

#include "mission/dialogs/AbstractDialogModel.h"

#include "mission/missionparse.h"
#include "nebula/volumetrics.h"

#include <QString>
#include <QVector>
#include <utility>

namespace fso::fred::dialogs {

class VolumetricNebulaDialogModel : public AbstractDialogModel {
	Q_OBJECT

public:
	VolumetricNebulaDialogModel(QObject* parent, EditorViewport* viewport);

	// overrides
	bool apply() override;
	void reject() override;

	QByteArray captureState() const override;
	void restoreState(const QByteArray& state) override;

	// Snapshot/restore of the mission globals this dialog edits. Static so an
	// undo command can restore them with no dialog instance alive.
	static QByteArray captureGlobalState();
	static void restoreGlobalState(const QByteArray& state);
	// The globals as they were when the dialog opened, before any live preview.
	// This is the "before" of the dialog's OK.
	QByteArray captureOriginalState() const;

	// The viewport gizmo only ever changes the position. These snapshot just
	// that, so undoing a drag can't clobber anything else.
	static QByteArray captureGizmoState();
	static void restoreGizmoState(const QByteArray& state);
	// Re-read the position from The_mission.volumetrics after a gizmo moved it.
	// includeBaseline also moves the Cancel baseline, for edits that happened
	// outside this dialog (a main-stack undo).
	void syncGizmoFromGlobals(bool includeBaseline);

	// limits
	static std::pair<float, float> getOpacityLimit()                { return {0.0001f, 1.0f}; }
	static std::pair<float, float> getOpacityDistanceLimit()        { return {0.1f, 16777215.0f}; } // Qt max
	static std::pair<int,   int>   getStepsLimit()                  { return {1, 100}; }
	static std::pair<int,   int>   getResolutionLimit()             { return {5, 8}; }
	static std::pair<int,   int>   getOversamplingLimit()           { return {1, 3}; }
	static std::pair<float, float> getSmoothingLimit()              { return {0.0f, 0.5f}; }
	static std::pair<float, float> getHenyeyGreensteinLimit()       { return {-1.0f, 1.0f}; }
	static std::pair<float, float> getSunFalloffFactorLimit()       { return {0.001f, 100.0f}; }
	static std::pair<int,   int>   getSunStepsLimit()               { return {2, 16}; }
	static std::pair<float, float> getEmissiveSpreadLimit()         { return {0.0f, 5.0f}; }
	static std::pair<float, float> getEmissiveIntensityLimit()      { return {0.0f, 100.0f}; }
	static std::pair<float, float> getEmissiveFalloffLimit()        { return {0.01f, 10.0f}; }
	static std::pair<float, float> getNoiseScaleBaseLimit()         { return {0.01f, 1000.0f}; }
	static std::pair<float, float> getNoiseScaleSubLimit()          { return {0.01f, 1000.0f}; }
	static std::pair<float, float> getNoiseIntensityLimit()         { return {0.1f, 100.0f}; }
	static std::pair<int,   int>   getNoiseResolutionLimit()        { return {5, 8}; }

	bool getEnabled() const;
    void setEnabled(bool e);

    // Basic
	const SCP_string& getHullPof() const;
    void setHullPof(const SCP_string& pofPath);

    float getPosX() const;
    void setPosX(float x);
	float getPosY() const;
    void setPosY(float y);
	float getPosZ() const;
    void setPosZ(float z);

    // Color
	int getColorR() const;
	void setColorR(int r);
	int getColorG() const;
	void setColorG(int g);
	int getColorB() const;
	void setColorB(int b);

    // Visibility
	float getOpacity() const;
    void setOpacity(float v);

    float getOpacityDistance() const;
    void setOpacityDistance(float v);

    // Quality
	int getSteps() const;
    void setSteps(int v);

    int getResolution() const;
    void setResolution(int v);

    int getOversampling() const;
    void setOversampling(int v);

    float getSmoothing() const;
    void setSmoothing(float v);

    // Lighting
	float getHenyeyGreenstein() const;
    void setHenyeyGreenstein(float v);

    float getSunFalloffFactor() const;
    void setSunFalloffFactor(float v);

    int getSunSteps() const;
    void setSunSteps(int v);

    // Emissive
	float getEmissiveSpread() const;
    void setEmissiveSpread(float v);

    float getEmissiveIntensity() const;
    void setEmissiveIntensity(float v);

    float getEmissiveFalloff() const;
    void setEmissiveFalloff(float v);

    // Noise
	bool getNoiseEnabled() const;
    void setNoiseEnabled(bool on);

    int getNoiseColorR() const;
	void setNoiseColorR(int r);
	int getNoiseColorG() const;
	void setNoiseColorG(int g);
	int getNoiseColorB() const;
	void setNoiseColorB(int b);

    float getNoiseScaleBase() const;
    void setNoiseScaleBase(float v);
	float getNoiseScaleSub() const;
    void setNoiseScaleSub(float v);

    float getNoiseIntensity() const;
    void setNoiseIntensity(float v);

    int getNoiseResolution() const;
    void setNoiseResolution(int v);

signals:
	// A viewport gizmo edit finished while this dialog is open. The dialog
	// records it on its own undo stack, so OK keeps it and Cancel drops it.
	void gizmoEditCommitted(const QByteArray& before, const QByteArray& after, const QString& text);

private:
	// Re-read the whole working copy after an undo rewrote the globals behind
	// this dialog's back.
	void reloadFromGlobals();

	void initializeData();
	bool validate_data();
	void showErrorDialogNoCancel(const SCP_string& message);

	// Push the working position into The_mission.volumetrics so the
	// translucent hull and the gizmo follow the spinboxes. reject() restores
	// the original.
	void pushLivePos();

	static void makeVolumetricsCopy(volumetric_nebula& dest, const volumetric_nebula& src);

	// boilerplate
	bool _bypass_errors;

	volumetric_nebula _volumetrics;

	// Snapshot of the original mission volumetrics state at dialog-open time
	// so reject() can roll back live-preview changes (currently: position).
	bool _had_original_volumetrics = false;
	volumetric_nebula _original_volumetrics;
};

} // namespace fso::fred::dialogs
