#include "../src/AnchorSmoother.h"
#include "../src/WeaponIconDrawing.h"
#include "../src/TrailDrawing.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
	std::ostringstream svg;
	std::vector<ImVec2> path;
	int primitives = 0, checks = 0;
	void Check(bool value, const char* why) { ++checks; if (!value) { throw std::runtime_error(why); } }
	std::string Colour(ImU32 c) {
		std::ostringstream s;
		s << '#' << std::hex << std::setfill('0') << std::setw(2) << (c & 255)
			<< std::setw(2) << ((c >> 8) & 255) << std::setw(2) << ((c >> 16) & 255);
		return s.str();
	}
	void Poly(const ImVec2* p, int count, ImU32 colour, bool filled, bool closed, float width) {
		++primitives;
		svg << (closed ? "<polygon points=\"" : "<polyline points=\"");
		for (int i = 0; i < count; ++i) { svg << p[i].x << ',' << p[i].y << ' '; }
		svg << "\" fill=\"" << (filled ? Colour(colour) : "none") << "\" stroke=\""
			<< Colour(colour) << "\" stroke-width=\"" << (filled ? 0 : width) << "\"/>\n";
	}
}

// Capture the production geometry's cimgui draw calls without loading Skyrim
// or the framework. The SVG is a geometry/layout fixture, not a game screenshot.
extern "C" {
void ImDrawList_AddLine(ImDrawList*, ImVec2 a, ImVec2 b, ImU32 c, float w) { const ImVec2 p[]{a,b}; Poly(p,2,c,false,false,w); }
void ImDrawList_AddCircle(ImDrawList*, ImVec2 at, float r, ImU32 c, int, float w) {
	++primitives; svg << "<circle cx=\"" << at.x << "\" cy=\"" << at.y << "\" r=\"" << r
		<< "\" fill=\"none\" stroke=\"" << Colour(c) << "\" stroke-width=\"" << w << "\"/>\n";
}
void ImDrawList_AddCircleFilled(ImDrawList*, ImVec2 at, float r, ImU32 c, int) {
	++primitives; svg << "<circle cx=\"" << at.x << "\" cy=\"" << at.y << "\" r=\"" << r << "\" fill=\"" << Colour(c) << "\"/>\n";
}
void ImDrawList_AddConvexPolyFilled(ImDrawList*, const ImVec2* p, int n, ImU32 c) { Poly(p,n,c,true,true,0); }
void ImDrawList_AddTriangleFilled(ImDrawList*, ImVec2 a, ImVec2 b, ImVec2 c, ImU32 colour) { const ImVec2 p[]{a,b,c}; Poly(p,3,colour,true,true,0); }
void ImDrawList_AddPolyline(ImDrawList*, const ImVec2* p, int n, ImU32 c, ImDrawFlags f, float w) { Poly(p,n,c,false,(f & ImDrawFlags_Closed)!=0,w); }
void ImDrawList_PathArcTo(ImDrawList*, ImVec2 c, float r, float first, float last, int steps) {
	for (int i = 0; i <= steps; ++i) { const float a = first + (last-first)*i/steps; path.push_back({c.x+r*std::cos(a),c.y+r*std::sin(a)}); }
}
void ImDrawList_PathStroke(ImDrawList*, ImU32 c, ImDrawFlags f, float w) { Poly(path.data(),static_cast<int>(path.size()),c,false,(f & ImDrawFlags_Closed)!=0,w); path.clear(); }
}

int main() try {
	using namespace SS;
	AnchorSmoother smoother;
	Check(smoother.Update(1,{10,20},1,.2f).x==10,"first point snaps");
	const auto actor=smoother.Update(1,{110,20},1.1f,.2f);
	Check(actor.x>10 && actor.x<110,"moving character is smoothed");
	Check(smoother.Update(2,{10,20},1,0).x==10 && smoother.Update(2,{110,20},1.1f,0).x==110,"camera rotation on static item has no smoothing lag");
	Check(smoother.Update(1,{160,20},1.2f,0).x==160,"scope toggle bypasses immediately");
	Check(smoother.Update(1,{170,20},1.21f,.2f).x==170,"scope toggle clears stale cache");
	Check(smoother.Update(1,{600,20},1.22f,.2f).x==600,"camera cut snaps");
	Check(smoother.Update(1,{610,20},3,.2f).x==610,"return after pause snaps");
	Check(smoother.Update(0,{123,20},3,.2f).x==123,"unknown owner is not eased");
	smoother.Prune(10);
	Check(smoother.Update(1,{640,20},10.1f,.2f).x==640,"pruned owner restarts without lag");

	// All ordered type combinations, including same-type dual wielding.
	for (unsigned l=0;l<static_cast<unsigned>(WeaponKind::kCount);++l) {
		for (unsigned r=0;r<static_cast<unsigned>(WeaponKind::kCount);++r) {
			const HandIcon left{static_cast<WeaponKind>(l),0xCC6E3A,.25f}, right{static_cast<WeaponKind>(r),0x9CC0D1,.75f};
			const auto icons=ComposeEquipmentIcons(left,right);
			if (IsTwoHandedIcon(right.kind)) { Check(icons.count==1 && icons.icons[0].kind==right.kind && icons.icons[0].fill==.75f,"two-handed right item is not duplicated"); }
			else if (IsTwoHandedIcon(left.kind)) { Check(icons.count==1 && icons.icons[0].kind==left.kind && icons.icons[0].fill==.25f,"two-handed left item is not duplicated"); }
			else if (l && r) { Check(icons.count==2 && icons.icons[0].kind==left.kind && icons.icons[1].kind==right.kind,"ordered dual equipment survives"); Check(icons.icons[0].colour==left.colour && icons.icons[1].colour==right.colour && icons.icons[0].fill==.25f && icons.icons[1].fill==.75f,"each hand retains its own colour and charge"); }
			else { Check(icons.count==1 && icons.icons[0].kind==(l?left.kind:r?right.kind:WeaponKind::kFists),"single hand and unarmed fallback"); }
			unsigned drawn=0;
			ForEachEquipmentIcon(icons,100,50,20,[&](const HandIcon&,float x,float y,float size) {
				Check(x-size*.5f>=100-icons.Width(20)*.5f-.001f && x+size*.5f<=100+icons.Width(20)*.5f+.001f && y==50,"glyph fits reserved width"); ++drawn;
			});
			Check(drawn==icons.count,"every reserved icon is drawn");
		}
	}
	const auto independent=ComposeEquipmentIcons({WeaponKind::kSword,0,-1},{WeaponKind::kSword,0xCC6E3A,.3f});
	Check(independent.count==2 && independent.icons[0].colour==0 && independent.icons[0].fill==-1,"unenchanted hand never borrows the other enchantment");
	for (unsigned k=1;k<static_cast<unsigned>(WeaponKind::kCount);++k) {
		const int before=primitives; DrawWeaponIcon(nullptr,static_cast<WeaponKind>(k),{50,50},20,0xFFFFFFFF);
		Check(primitives>before,"every supported kind has drawable geometry");
	}
	for (const auto direction : { ImVec2{1,0}, ImVec2{0,1}, ImVec2{-1,0}, ImVec2{0,-1}, ImVec2{.6f,.8f} }) {
		const auto right = FootprintQuad({100,200}, direction, 20, false);
		const auto left = FootprintQuad({100,200}, direction, 20, true);
		for (int i = 0; i < 4; ++i) {
			Check(right.points[i].x == left.points[i].x && right.points[i].y == left.points[i].y &&
				right.uv[i].x == 1-left.uv[i].x && right.uv[i].y == left.uv[i].y, "left footprint mirrors art without changing quad");
		}
		const ImVec2 top{(right.points[0].x+right.points[1].x)*.5f, (right.points[0].y+right.points[1].y)*.5f};
		Check(std::abs(top.x-100-direction.x*13)<.001f && std::abs(top.y-200-direction.y*13)<.001f,
			"image toes point along travel for every tested direction");
		const float dx=right.points[1].x-right.points[0].x, dy=right.points[1].y-right.points[0].y;
		Check(std::abs(dx*dx+dy*dy-26*26)<.01f, "footprint square preserves artwork aspect");
	}
	svg.str(""); svg.clear();
	svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"760\" viewBox=\"0 0 960 760\">\n<rect width=\"960\" height=\"760\" fill=\"#161c26\"/>\n";
	svg << "<text x=\"20\" y=\"28\" fill=\"#e7edf5\" font-family=\"Arial\" font-size=\"18\">Production glyph geometry / left hand then right hand</text>\n";
	struct Example { const char* label; WeaponKind left, right; };
	const Example examples[]{
		{"Unarmed",WeaponKind::kNone,WeaponKind::kNone}, {"Dagger + sword",WeaponKind::kDagger,WeaponKind::kSword},
		{"Axe + mace",WeaponKind::kAxe,WeaponKind::kMace}, {"Spell + sword",WeaponKind::kSpell,WeaponKind::kSword},
		{"Sword + spell",WeaponKind::kSword,WeaponKind::kSpell}, {"Dual spells",WeaponKind::kSpell,WeaponKind::kSpell},
		{"Dual staves",WeaponKind::kStaff,WeaponKind::kStaff}, {"Staff + sword",WeaponKind::kStaff,WeaponKind::kSword},
		{"Shield + sword",WeaponKind::kShield,WeaponKind::kSword}, {"Torch + axe",WeaponKind::kTorch,WeaponKind::kAxe},
		{"Bow (one icon)",WeaponKind::kBow,WeaponKind::kBow}, {"Greatsword (one icon)",WeaponKind::kSpell,WeaponKind::kGreatsword},
		{"Crossbow (one icon)",WeaponKind::kCrossbow,WeaponKind::kNone}, {"Right hand only",WeaponKind::kNone,WeaponKind::kSword},
		{"Spell + staff",WeaponKind::kSpell,WeaponKind::kStaff}
	};
	unsigned row=0;
	for (const auto& e:examples) {
		const float x=160.f+320.f*(row%3), y=105.f+140.f*(row/3); ++row;
		svg << "<text x=\"" << x-130 << "\" y=\"" << y-45 << "\" fill=\"#e7edf5\" font-family=\"Arial\" font-size=\"16\">" << e.label << "</text>\n";
		const auto icons=ComposeEquipmentIcons({e.left,0xCC6E3A,.25f},{e.right,0x9CC0D1,.75f});
		ForEachEquipmentIcon(icons,x,y,50,[](const HandIcon& icon,float px,float py,float size) {
			const auto c=icon.colour; const ImU32 packed=0xFF000000 | ((c>>16)&255) | (c&0x00FF00) | ((c&255)<<16);
			DrawWeaponIcon(nullptr,icon.kind,{px,py},size,c?packed:0xFFE7EDF5);
		});
	}
	svg << "</svg>\n";
	std::ofstream("equipment-icons.svg") << svg.str();
	std::cout << checks << " display checks passed; 196 ordered combinations; 15 production-geometry fixtures\n";
	return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
