#pragma once

// Our Assert conflicts with the definitions inside libRocket
#pragma push_macro("Assert")
#undef Assert

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include <Rocket/Core/Element.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#pragma pop_macro("Assert")

namespace scpui {
namespace elements {

using namespace Rocket::Core;

class BriefingRevealElement : public Rocket::Core::Element {
  public:
	BriefingRevealElement(const String& tag_in);
	~BriefingRevealElement() override;

  protected:
	void OnAttributeChange(const AttributeNameList& changed_attributes) override;
	void OnChildAdd(Element* child) override;
	void OnChildRemove(Element* child) override;
	void OnBeforeRender() override;
	void OnAfterRender() override;

  private:
	void resetAnimation();

	float _duration = 1.0f;
	float _angle = -30.0f;
	float _cell_size = 16.0f;
	float _edge_width = 10.0f;
	float _edge_fade = 18.0f;
	float _animation_start_time = -1.0f;
};

} // namespace elements
} // namespace scpui
