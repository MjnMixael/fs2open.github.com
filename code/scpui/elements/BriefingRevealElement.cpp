#include "BriefingRevealElement.h"

#include "scpui/RocketRenderingInterface.h"

// Our Assert conflicts with the definitions inside libRocket
#pragma push_macro("Assert")
#undef Assert

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include <Rocket/Core.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#pragma pop_macro("Assert")

namespace scpui {
namespace elements {

BriefingRevealElement::BriefingRevealElement(const String& tag_in) : Element(tag_in)
{
}

BriefingRevealElement::~BriefingRevealElement() = default;

void BriefingRevealElement::OnAttributeChange(const AttributeNameList& changed_attributes)
{
	Element::OnAttributeChange(changed_attributes);

	if (changed_attributes.find("duration") != changed_attributes.end()) {
		_duration = GetAttribute<float>("duration", _duration);
	}
	if (changed_attributes.find("angle") != changed_attributes.end()) {
		_angle = GetAttribute<float>("angle", _angle);
	}
	if (changed_attributes.find("cell-size") != changed_attributes.end()) {
		_cell_size = GetAttribute<float>("cell-size", _cell_size);
	}
	if (changed_attributes.find("edge-width") != changed_attributes.end()) {
		_edge_width = GetAttribute<float>("edge-width", _edge_width);
	}
	if (changed_attributes.find("edge-fade") != changed_attributes.end()) {
		_edge_fade = GetAttribute<float>("edge-fade", _edge_fade);
	}
}

void BriefingRevealElement::OnChildAdd(Element* child)
{
	Element::OnChildAdd(child);
	resetAnimation();
}

void BriefingRevealElement::OnChildRemove(Element* child)
{
	Element::OnChildRemove(child);
	resetAnimation();
}

void BriefingRevealElement::resetAnimation()
{
	_animation_start_time = Rocket::Core::GetSystemInterface()->GetElapsedTime();
}

void BriefingRevealElement::OnBeforeRender()
{
	Assertion(dynamic_cast<RocketRenderingInterface*>(GetRenderInterface()) != nullptr,
		"This element can only be used with our custom render interface!");
	auto* renderInterface = static_cast<RocketRenderingInterface*>(GetRenderInterface());

	if (_animation_start_time < 0.0f) {
		resetAnimation();
	}

	const auto now = Rocket::Core::GetSystemInterface()->GetElapsedTime();
	float progress = _duration > 0.0f ? static_cast<float>((now - _animation_start_time) / _duration) : 1.0f;
	if (progress < 0.0f) {
		progress = 0.0f;
	} else if (progress > 1.0f) {
		progress = 1.0f;
	}

	const auto abs = GetAbsoluteOffset(Box::CONTENT);
	const auto size = GetBox().GetSize(Box::CONTENT);

	renderInterface->setBriefingRevealState(true,
		Rocket::Core::Vector2f(abs.x, abs.y),
		Rocket::Core::Vector2f(size.x, size.y),
		progress,
		_angle,
		_cell_size,
		_edge_width,
		_edge_fade);
}

void BriefingRevealElement::OnAfterRender()
{
	Assertion(dynamic_cast<RocketRenderingInterface*>(GetRenderInterface()) != nullptr,
		"This element can only be used with our custom render interface!");
	auto* renderInterface = static_cast<RocketRenderingInterface*>(GetRenderInterface());
	renderInterface->setBriefingRevealState(false,
		Rocket::Core::Vector2f(0.f, 0.f),
		Rocket::Core::Vector2f(0.f, 0.f),
		1.0f,
		_angle,
		_cell_size,
		_edge_width,
		_edge_fade);
}

} // namespace elements
} // namespace scpui
