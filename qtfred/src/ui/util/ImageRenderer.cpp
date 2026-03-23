#include "ImageRenderer.h"

#include <bmpman/bmpman.h> // bm_load, bm_get_info, bm_lock

#include <QtGlobal>

namespace fso::fred::util {

static void setError(QString* outError, const QString& text)
{
	if (outError)
		*outError = text;
}

bool loadHandleToQImage(int bmHandle, QImage& outImage, QString* outError)
{
	outImage = QImage(); // clear

	if (bmHandle < 0) {
		setError(outError, QStringLiteral("Invalid bitmap handle."));
		return false;
	}

	int w = 0, h = 0;
	ushort flags = 0;
	int nframes = 0, fps = 0;

	// Use the returned handle (first frame if this is an animation.. TODO: Handle animations. Will be useful for Heads)
	int srcHandle = bm_get_info(bmHandle, &w, &h, &flags, &nframes, &fps);
	if (srcHandle < 0 || w <= 0 || h <= 0) {
		setError(outError, QStringLiteral("Bitmap has invalid info."));
		return false;
	}

	if (w <= 0 || h <= 0) {
		setError(outError, QStringLiteral("Bitmap has invalid dimensions."));
		return false;
	}

	auto* bmp = bm_lock(bmHandle, 32, BMP_TEX_XPARENT);
	if (bmp == nullptr || bmp->data == 0) {
		setError(outError, QStringLiteral("bm_lock failed."));
		return false;
	}
	const int bytesPerLine = static_cast<int>(bmp->rowsize);
	QImage tmp(reinterpret_cast<const uchar*>(bmp->data), w, h, bytesPerLine, QImage::Format_ARGB32);
	outImage = tmp.copy();
	bm_unlock(bmHandle);

	if (outImage.isNull()) {
		setError(outError, QStringLiteral("Failed to construct QImage."));
		return false;
	}

	return true;
}

bool loadImageToQImage(const std::string& filename, QImage& outImage, QString* outError)
{
	outImage = QImage();

	if (filename.empty()) {
		setError(outError, QStringLiteral("Empty filename."));
		return false;
	}

	// Let bmpman resolve the file
	int handle = bm_load(filename.c_str());
	if (handle < 0) {
		setError(outError, QStringLiteral("bm_load failed for \"%1\".").arg(QString::fromStdString(filename)));
		return false;
	}

	const bool ok = loadHandleToQImage(handle, outImage, outError);


	// bm_unload(handle); TODO test unloading

	return ok;
}

} // namespace fso::fred::util
