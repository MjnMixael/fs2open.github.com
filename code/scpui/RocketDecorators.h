#pragma once

// Our Assert conflicts with the definitions inside libRocket
#pragma push_macro("Assert")
#undef Assert

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include <Rocket/Core.h>
#include <Rocket/Core/Decorator.h>
#include <Rocket/Core/Element.h>
#include <Rocket/Core/Geometry.h>
#include <Rocket/Core/GeometryUtilities.h>
#include <Rocket/Core/Property.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#pragma pop_macro("Assert")

namespace scpui {
namespace decorators {

enum class LineStyle {
	Solid,
	Dashed,
	Dotted,
	Num_styles
};

class DecoratorUnderline : public Rocket::Core::Decorator {
  public:
	// Constructor to accept properties
	// Lengths keep their units and are resolved against each decorated element's content width when rendering
	DecoratorUnderline(const Rocket::Core::Property& line_thickness, LineStyle line_style, const Rocket::Core::Property& line_length, const Rocket::Core::Property& line_space, Rocket::Core::Colourb line_color, bool use_element_color);
	~DecoratorUnderline() override;

	// Called to generate per-element data for newly decorated elements.
	Rocket::Core::DecoratorDataHandle GenerateElementData(Rocket::Core::Element* element) override;

	// Called to release element-specific data.
	void ReleaseElementData(Rocket::Core::DecoratorDataHandle element_data) override;

	// Called to render the decorator on an element.
	void RenderElement(Rocket::Core::Element* element, Rocket::Core::DecoratorDataHandle element_data) override;

  private:
	Rocket::Core::Property line_thickness;
	LineStyle line_style;
	Rocket::Core::Property line_length;
	Rocket::Core::Property line_space;
	Rocket::Core::Colourb line_color;
	bool use_element_color;
};

class DecoratorCornerBorders : public Rocket::Core::Decorator {
  public:
	// Constructor to accept properties
	// Lengths keep their units and are resolved against each decorated element's content box when rendering:
	// thickness and the horizontal length against its width, the vertical length against its height
	DecoratorCornerBorders(const Rocket::Core::Property& border_thickness, const Rocket::Core::Property& border_length_h, const Rocket::Core::Property& border_length_v, Rocket::Core::Colourb border_color);
	~DecoratorCornerBorders() override;

	// Called to generate per-element data for newly decorated elements.
	Rocket::Core::DecoratorDataHandle GenerateElementData(Rocket::Core::Element* element) override;

	// Called to release element-specific data.
	void ReleaseElementData(Rocket::Core::DecoratorDataHandle element_data) override;

	// Called to render the decorator on an element.
	void RenderElement(Rocket::Core::Element* element, Rocket::Core::DecoratorDataHandle element_data) override;

  private:
	Rocket::Core::Property border_thickness;
	Rocket::Core::Property border_length_h;
	Rocket::Core::Property border_length_v;
	Rocket::Core::Colourb border_color;
};

} // namespace decorators
} // namespace scpui
