#include "RocketDecoratorsInstancer.h"

#include <Rocket/Core/Colour.h>
#include <Rocket/Core/PropertyParser.h>
#include <Rocket/Core/StyleSheetSpecification.h>

namespace scpui {
namespace decorators {

// UnderlineDecoratorInstancer Implementation
UnderlineDecoratorInstancer::UnderlineDecoratorInstancer()
{
	RegisterProperty("thickness", "1.0").AddParser("number");
	RegisterProperty("style", "solid").AddParser("keyword", "solid, dashed, dotted");
	RegisterProperty("length", "5.0").AddParser("number");
	RegisterProperty("space", "3.0").AddParser("number");
	RegisterProperty("color-setting", "default").AddParser("keyword", "default, element");
	RegisterProperty("color", "white").AddParser("color");

	RegisterShorthand("shorthand", "style, thickness, length, space");
}

Rocket::Core::Decorator* UnderlineDecoratorInstancer::InstanceDecorator(const Rocket::Core::String& /*name*/,
	const Rocket::Core::PropertyDictionary& prop_dict)
{
	// librocket documentation says the best way to get a keyword is by index so get
	// that and convert it for readability to an enum for downstream methods	
	int style_idx = prop_dict.GetProperty("style")->Get<int>();
	LineStyle style;
	if (style_idx < 0 || style_idx >= static_cast<int>(LineStyle::Num_styles)) {
		style = LineStyle::Solid;
	} else {
		style = static_cast<LineStyle>(style_idx);
	}

	// Lengths are passed with their units so the decorator can resolve them per element
	const auto& thickness = *prop_dict.GetProperty("thickness");
	const auto& length = *prop_dict.GetProperty("length");
	const auto& space = *prop_dict.GetProperty("space");
	Rocket::Core::Colourb color = prop_dict.GetProperty("color")->Get<Rocket::Core::Colourb>();
	bool use_element_color = prop_dict.GetProperty("color-setting")->Get<int>();

	return new DecoratorUnderline(thickness, style, length, space, color, use_element_color);
}

void UnderlineDecoratorInstancer::ReleaseDecorator(Rocket::Core::Decorator* decorator)
{
	delete decorator;
}

void UnderlineDecoratorInstancer::Release()
{
	delete this;
}

// BorderDecoratorInstancer Implementation
BorderDecoratorInstancer::BorderDecoratorInstancer()
{
	// Register border properties
	RegisterProperty("thickness", "1.0").AddParser("number");
	RegisterProperty("length", "10.0").AddParser("number");
	// libRocket fills in every registered property's default before instancing, so the -1 default marks
	// "not specified" and the decorator falls back to "length"
	RegisterProperty("length-h", "-1").AddParser("number");
	RegisterProperty("length-v", "-1").AddParser("number");
	RegisterProperty("color", "white").AddParser("color");

	RegisterShorthand("shorthand", "thickness, length-h, length-v");
}

Rocket::Core::Decorator* BorderDecoratorInstancer::InstanceDecorator(const Rocket::Core::String& /*name*/,
	const Rocket::Core::PropertyDictionary& prop_dict)
{
	// Lengths are passed with their units so the decorator can resolve them per element
	const auto& thickness = *prop_dict.GetProperty("thickness");
	const auto& length = *prop_dict.GetProperty("length");

	// Use horizontal or vertical lengths if they were specified, and fall back to "length" otherwise
	auto specified = [](const Rocket::Core::Property& prop) { return prop.Get<float>() >= 0.0f; };
	const auto& length_h_prop = *prop_dict.GetProperty("length-h");
	const auto& length_v_prop = *prop_dict.GetProperty("length-v");
	const auto& length_h = specified(length_h_prop) ? length_h_prop : length;
	const auto& length_v = specified(length_v_prop) ? length_v_prop : length;

	// Fetch the border color, defaults to white if not set
	Rocket::Core::Colourb color = prop_dict.GetProperty("color")->Get<Rocket::Core::Colourb>();

	return new DecoratorCornerBorders(thickness, length_h, length_v, color);
}


void BorderDecoratorInstancer::ReleaseDecorator(Rocket::Core::Decorator* decorator)
{
	delete decorator;
}

void BorderDecoratorInstancer::Release()
{
	delete this;
}

} // namespace decorators
} // namespace scpui
