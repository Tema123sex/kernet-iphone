#ifndef GAME_CLIENT_COMPONENTS_ZZ_NEWYEARMENU_H
#define GAME_CLIENT_COMPONENTS_ZZ_NEWYEARMENU_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/client/component.h>

// Standalone New Year themed window. It only reads and writes existing config
// variables; it does not depend on any other client component.
class CZZNewYearMenu : public CComponent
{
public:
	enum ERowType
	{
		ROW_TOGGLE,
		ROW_SLIDER,
		ROW_NOTE,
	};

	struct SRowData;
	struct SSectionData;

	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

private:
	enum EHitKind
	{
		HIT_TOGGLE = 1,
		HIT_SLIDER,
		HIT_SEARCH,
	};

	struct SHit
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
		int m_Kind = 0;
		int CConfig::*m_pValue = nullptr;
		int m_Min = 0;
		int m_Max = 1;
		float m_TrackX = 0.0f;
		float m_TrackW = 1.0f;
	};

	struct SAnim
	{
		int CConfig::*m_pKey = nullptr;
		float m_On = 0.0f;
		float m_Hover = 0.0f;
	};

	struct SDrawContext
	{
		float m_Alpha = 1.0f;
		float m_Delta = 0.0f;
		float m_Time = 0.0f;
		bool m_Decor = true;
		ColorRGBA m_Accent = ColorRGBA(0.90f, 0.17f, 0.25f, 1.0f);
		ColorRGBA m_Green = ColorRGBA(0.22f, 0.74f, 0.38f, 1.0f);
		ColorRGBA m_Text = ColorRGBA(0.96f, 0.96f, 0.98f, 1.0f);
		ColorRGBA m_Muted = ColorRGBA(0.62f, 0.63f, 0.69f, 1.0f);
	};

	static constexpr int MAX_HITS = 64;
	static constexpr int MAX_ANIMS = 32;
	static constexpr int SEARCH_BUFFER = 48;

	SHit m_aHits[MAX_HITS];
	int m_NumHits = 0;
	SAnim m_aAnims[MAX_ANIMS];
	SDrawContext m_Ctx;

	vec2 m_MousePos = vec2(0.0f, 0.0f);
	float m_UiScale = 1.0f;
	float m_Anim = 0.0f;
	bool m_WasOpen = false;
	bool m_OpenedOverMenu = false;

	char m_aSearch[SEARCH_BUFFER] = "";
	bool m_SearchFocused = false;

	int CConfig::*m_pDragValue = nullptr;
	int m_DragMin = 0;
	int m_DragMax = 1;
	float m_DragTrackX = 0.0f;
	float m_DragTrackW = 1.0f;

	void CloseMenu();
	void Cleanup();
	void UpdateMousePosition();
	void HandleMouseDown();
	void UpdateDraggedSlider();
	void SetSearchFocus(bool Focus);

	bool PointInRect(vec2 Point, float X, float Y, float W, float H) const;
	void AddHit(const SHit &Hit);
	SAnim &AnimFor(int CConfig::*pKey, bool InitialOn);

	void DrawText(float X, float Y, float Size, const char *pText, const ColorRGBA &Color, bool RightAligned = false);
	void DrawTextFit(float X, float Y, float Size, const char *pText, const ColorRGBA &Color, float MaxWidth);
	void DrawWrapped(float X, float Y, float Size, const char *pText, const ColorRGBA &Color, float Width, int MaxLines);
	void DrawIcon(float X, float Y, float W, float H, float Size, const char *pIcon, const ColorRGBA &Color);
	void DrawBall(float CX, float CY, float Radius, const ColorRGBA &Color);
	void DrawSnowCap(float X, float Y, float W);
	void DrawSwitch(float X, float Y, float W, float H, float On, float Hover);

	float DrawToggleRow(float X, float Y, float W, const SRowData &Row);
	float DrawSliderRow(float X, float Y, float W, const SRowData &Row);
	float DrawNoteRow(float X, float Y, float W, const SRowData &Row);
	float SectionHeight(const SSectionData &Section) const;
	float DrawSection(float X, float Y, float W, const SSectionData &Section);
	void DrawHeader(float X, float Y, float W);
	void DrawSidebar(float X, float Y);
	void DrawSearchBar(float X, float Y, float W);
	void DrawSearchResults(float X, float Y, float W);
	void DrawSnow(float ScreenW, float ScreenH);
	void DrawPineFrame(float X, float Y, float W, float H);
};

#endif
