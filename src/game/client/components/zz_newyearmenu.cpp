#include "zz_newyearmenu.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/binds.h>
#include <game/client/components/console.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

struct CZZNewYearMenu::SRowData
{
	int m_Type;
	const char *m_pTitle;
	const char *m_pSub;
	int CConfig::*m_pValue;
	int m_Min;
	int m_Max;
	const char *m_pSuffix;
};

struct CZZNewYearMenu::SSectionData
{
	const char *m_pTitle;
	int m_Column;
	const SRowData *m_pRows;
	int m_NumRows;
};

namespace
{
	using SRow = CZZNewYearMenu::SRowData;
	using SSection = CZZNewYearMenu::SSectionData;

	constexpr int TOGGLE = CZZNewYearMenu::ROW_TOGGLE;
	constexpr int SLIDER = CZZNewYearMenu::ROW_SLIDER;
	constexpr int NOTE = CZZNewYearMenu::ROW_NOTE;

	constexpr float ROW_TOGGLE_H = 54.0f;
	constexpr float ROW_SLIDER_H = 54.0f;
	constexpr float ROW_NOTE_H = 70.0f;
	constexpr float SECTION_HEAD_H = 38.0f;
	constexpr float SECTION_PAD = 10.0f;
	constexpr float SECTION_GAP = 14.0f;

	const char *const PAGE_TITLE = "Автоанфриз";
	const char *const PAGE_DESC =
		"Пока вы не коснулись фриза, бот стреляет отскакивающим лазером: "
		"он возвращается и размораживает вас (нужен лазер).";

	const SRow s_aBehaviour[] = {
		{TOGGLE, "Тихий прицел",
			"Прицел меняется только в пакете ввода, камера не дёргается",
			&CConfig::m_ClZzAutoUnfreezeSilent, 0, 1, nullptr},
		{TOGGLE, "Предпочитать больше отскоков",
			"Из подходящих лазеров берёт тот, у которого больше рикошетов",
			&CConfig::m_ClZzAutoUnfreezeMostBounces, 0, 1, nullptr},
	};
	const SRow s_aAiming[] = {
		{SLIDER, "Угол обзора", nullptr,
			&CConfig::m_ClZzAutoUnfreezeFov, 5, 360, " °"},
		{SLIDER, "Максимум отскоков", nullptr,
			&CConfig::m_ClZzAutoUnfreezeMaxPoints, 1, 10, ""},
	};
	const SRow s_aTiming[] = {
		{SLIDER, "Реагировать на фриз за", nullptr,
			&CConfig::m_ClZzAutoUnfreezeDirTicks, 1, 40, " тиков"},
		{SLIDER, "Предсказание вперёд", nullptr,
			&CConfig::m_ClZzAutoUnfreezeTicks, 8, 50, " тиков"},
	};
	const SRow s_aTips[] = {
		{NOTE, nullptr,
			"Замороженный тии не может стрелять, а попадание внутри фриза "
			"замораживает снова — бот планирует это заранее. "
			"Совет: bind f8 toggle cl_zz_auto_unfreeze 0 1",
			nullptr, 0, 0, nullptr},
	};
	const SSection s_aSections[] = {
		{"ПОВЕДЕНИЕ", 0, s_aBehaviour, (int)std::size(s_aBehaviour)},
		{"ПРИЦЕЛИВАНИЕ", 0, s_aAiming, (int)std::size(s_aAiming)},
		{"ТАЙМИНГ", 1, s_aTiming, (int)std::size(s_aTiming)},
		{"ПОДСКАЗКА", 1, s_aTips, (int)std::size(s_aTips)},
	};

	float RowHeight(int Type)
	{
		if(Type == CZZNewYearMenu::ROW_SLIDER)
			return ROW_SLIDER_H;
		if(Type == CZZNewYearMenu::ROW_NOTE)
			return ROW_NOTE_H;
		return ROW_TOGGLE_H;
	}

	float Hash01(uint32_t Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		Value ^= Value >> 16;
		return (Value & 0xffffffU) / 16777215.0f;
	}

	float Approach(float Current, float Target, float Delta, float Response)
	{
		const float Blend = 1.0f - std::exp(-std::clamp(Delta, 0.0f, 0.05f) * Response);
		return Current + (Target - Current) * Blend;
	}
} // namespace

// ----------------------------------------------------------------------------
// Lifecycle and input
// ----------------------------------------------------------------------------

void CZZNewYearMenu::OnReset()
{
	m_pDragValue = nullptr;
	m_NumHits = 0;
}

void CZZNewYearMenu::SetSearchFocus(bool Focus)
{
	if(Focus == m_SearchFocused)
		return;
	m_SearchFocused = Focus;
	if(Focus)
		Input()->StartTextInput();
	else
		Input()->StopTextInput();
}

void CZZNewYearMenu::Cleanup()
{
	if(!m_WasOpen)
		return;
	m_WasOpen = false;
	m_pDragValue = nullptr;
	m_Anim = 0.0f;
	m_NumHits = 0;
	SetSearchFocus(false);
	if(GameClient()->m_Menus.IsActive() ||
		(!GameClient()->m_GameConsole.IsActive() && !GameClient()->m_Chat.IsActive()))
		Input()->MouseModeRelative();
	m_OpenedOverMenu = false;
}

void CZZNewYearMenu::CloseMenu()
{
	g_Config.m_ClZzNewYearMenu = 0;
	Cleanup();
}

void CZZNewYearMenu::UpdateMousePosition()
{
	if(!m_OpenedOverMenu)
		m_MousePos = Input()->NativeMousePos() / std::max(m_UiScale, 0.001f);
}

bool CZZNewYearMenu::PointInRect(vec2 Point, float X, float Y, float W, float H) const
{
	return Point.x >= X && Point.x <= X + W && Point.y >= Y && Point.y <= Y + H;
}

void CZZNewYearMenu::AddHit(const SHit &Hit)
{
	if(m_NumHits < MAX_HITS)
		m_aHits[m_NumHits++] = Hit;
}

CZZNewYearMenu::SAnim &CZZNewYearMenu::AnimFor(int CConfig::*pKey, bool InitialOn)
{
	for(SAnim &Anim : m_aAnims)
	{
		if(Anim.m_pKey == pKey)
			return Anim;
	}
	for(SAnim &Anim : m_aAnims)
	{
		if(!Anim.m_pKey)
		{
			Anim.m_pKey = pKey;
			Anim.m_On = InitialOn ? 1.0f : 0.0f;
			Anim.m_Hover = 0.0f;
			return Anim;
		}
	}
	return m_aAnims[MAX_ANIMS - 1];
}

void CZZNewYearMenu::UpdateDraggedSlider()
{
	if(!m_pDragValue)
		return;
	const float Ratio = std::clamp(
		(m_MousePos.x - m_DragTrackX) / std::max(m_DragTrackW, 1.0f), 0.0f, 1.0f);
	const int Value = m_DragMin + (int)std::lround(Ratio * (float)(m_DragMax - m_DragMin));
	g_Config.*m_pDragValue = std::clamp(Value, m_DragMin, m_DragMax);
}

void CZZNewYearMenu::HandleMouseDown()
{
	UpdateMousePosition();
	for(int i = m_NumHits - 1; i >= 0; --i)
	{
		const SHit &Hit = m_aHits[i];
		if(!PointInRect(m_MousePos, Hit.m_X, Hit.m_Y, Hit.m_W, Hit.m_H))
			continue;
		if(Hit.m_Kind == HIT_SEARCH)
		{
			SetSearchFocus(true);
			return;
		}
		SetSearchFocus(false);
		if(Hit.m_Kind == HIT_TOGGLE && Hit.m_pValue)
			g_Config.*(Hit.m_pValue) = g_Config.*(Hit.m_pValue) ? 0 : 1;
		else if(Hit.m_Kind == HIT_SLIDER)
		{
			m_pDragValue = Hit.m_pValue;
			m_DragMin = Hit.m_Min;
			m_DragMax = Hit.m_Max;
			m_DragTrackX = Hit.m_TrackX;
			m_DragTrackW = Hit.m_TrackW;
			UpdateDraggedSlider();
		}
		return;
	}
	SetSearchFocus(false);
}

bool CZZNewYearMenu::OnInput(const IInput::CEvent &Event)
{
	const bool Open = g_Config.m_ClZzNewYearMenu != 0;
	const bool Pressed = (Event.m_Flags & IInput::FLAG_PRESS) != 0;
	const bool FreshPress = Pressed && (Event.m_Flags & IInput::FLAG_REPEAT) == 0;
	const bool IsText = (Event.m_Flags & IInput::FLAG_TEXT) != 0;

	const int ModifierMask = CBinds::GetModifierMask(Input()) &
				 ~CBinds::GetModifierMaskOfKey(Event.m_Key);
	if(FreshPress && !IsText && g_Config.m_ClZzNewYearMenuKey != KEY_UNKNOWN &&
		Event.m_Key == g_Config.m_ClZzNewYearMenuKey && ModifierMask == 0)
	{
		if(Open)
		{
			CloseMenu();
			return true;
		}
		if(!GameClient()->m_Chat.IsActive() && !GameClient()->m_GameConsole.IsActive())
		{
			g_Config.m_ClZzNewYearMenu = 1;
			return true;
		}
	}
	if(!Open)
		return false;

	if(m_SearchFocused)
	{
		if(IsText)
		{
			const int Length = str_length(m_aSearch);
			const int Add = str_length(Event.m_aText);
			if(Length + Add < SEARCH_BUFFER - 1)
				str_append(m_aSearch, Event.m_aText, sizeof(m_aSearch));
			return true;
		}
		if(Pressed && Event.m_Key == KEY_BACKSPACE)
		{
			const int Length = str_length(m_aSearch);
			if(Length > 0)
				m_aSearch[str_utf8_rewind(m_aSearch, Length)] = '\0';
			return true;
		}
		if(FreshPress && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
		{
			SetSearchFocus(false);
			return true;
		}
	}

	if(Pressed && Event.m_Key == KEY_ESCAPE)
	{
		if(m_SearchFocused)
			SetSearchFocus(false);
		else if(m_aSearch[0] != '\0')
			m_aSearch[0] = '\0';
		else
			CloseMenu();
		return true;
	}
	if(Pressed && Event.m_Key == KEY_MOUSE_1)
		HandleMouseDown();
	else if((Event.m_Flags & IInput::FLAG_RELEASE) && Event.m_Key == KEY_MOUSE_1)
		m_pDragValue = nullptr;
	return true;
}

bool CZZNewYearMenu::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(g_Config.m_ClZzNewYearMenu == 0)
		return false;
	if(m_OpenedOverMenu)
	{
		Ui()->ConvertMouseMove(&x, &y, CursorType);
		Ui()->OnCursorMove(x, y);
		const CUIRect *pUiScreen = Ui()->Screen();
		if(pUiScreen->w > 1.0f && pUiScreen->h > 1.0f)
		{
			const vec2 Normalized = (Ui()->MousePos() - vec2(pUiScreen->x, pUiScreen->y)) /
						vec2(pUiScreen->w, pUiScreen->h);
			m_MousePos = Normalized * vec2(Graphics()->ScreenWidth() / m_UiScale,
							 Graphics()->ScreenHeight() / m_UiScale);
		}
	}
	else
		UpdateMousePosition();
	return true;
}

// ----------------------------------------------------------------------------
// Drawing primitives
// ----------------------------------------------------------------------------

void CZZNewYearMenu::DrawText(float X, float Y, float Size, const char *pText,
	const ColorRGBA &Color, bool RightAligned)
{
	if(RightAligned)
		X -= TextRender()->TextWidth(Size, pText);
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(X, Y));
	Cursor.m_FontSize = Size;
	TextRender()->TextColor(Color);
	TextRender()->TextEx(&Cursor, pText, -1);
}

void CZZNewYearMenu::DrawTextFit(float X, float Y, float Size, const char *pText,
	const ColorRGBA &Color, float MaxWidth)
{
	char aBuf[256];
	str_copy(aBuf, pText, sizeof(aBuf));
	if(TextRender()->TextWidth(Size, aBuf) > MaxWidth)
	{
		int Length = str_length(aBuf);
		while(Length > 1 && TextRender()->TextWidth(Size, aBuf) > MaxWidth)
		{
			Length = str_utf8_rewind(aBuf, Length);
			aBuf[Length] = '\0';
		}
		if(Length > 2)
		{
			Length = str_utf8_rewind(aBuf, Length);
			aBuf[Length] = '\0';
			str_append(aBuf, "...", sizeof(aBuf));
		}
	}
	DrawText(X, Y, Size, aBuf, Color);
}

void CZZNewYearMenu::DrawWrapped(float X, float Y, float Size, const char *pText,
	const ColorRGBA &Color, float Width, int MaxLines)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(X, Y));
	Cursor.m_FontSize = Size;
	Cursor.m_LineWidth = Width;
	Cursor.m_MaxLines = MaxLines;
	TextRender()->TextColor(Color);
	TextRender()->TextEx(&Cursor, pText, -1);
}

void CZZNewYearMenu::DrawIcon(float X, float Y, float W, float H, float Size,
	const char *pIcon, const ColorRGBA &Color)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const STextBoundingBox Bounds = TextRender()->TextBoundingBox(Size, pIcon);
	TextRender()->TextColor(Color);
	TextRender()->Text(X + (W - Bounds.m_W) * 0.5f, Y + (H - Bounds.m_H) * 0.5f, Size, pIcon);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CZZNewYearMenu::DrawBall(float CX, float CY, float Radius, const ColorRGBA &Color)
{
	const float A = m_Ctx.m_Alpha;
	Graphics()->DrawRect(CX - 2.5f, CY - Radius - 4.0f, 5.0f, 5.0f,
		ColorRGBA(0.92f, 0.76f, 0.30f, A), IGraphics::CORNER_T, 1.5f);
	Graphics()->DrawRect(CX - Radius, CY - Radius, Radius * 2.0f, Radius * 2.0f,
		Color.WithAlpha(A), IGraphics::CORNER_ALL, Radius);
	Graphics()->DrawRect(CX - Radius * 0.55f, CY - Radius * 0.65f, Radius * 0.6f, Radius * 0.6f,
		ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f * A), IGraphics::CORNER_ALL, Radius * 0.3f);
}

void CZZNewYearMenu::DrawSnowCap(float X, float Y, float W)
{
	if(!m_Ctx.m_Decor)
		return;
	const float A = m_Ctx.m_Alpha;
	const ColorRGBA Snow(0.96f, 0.98f, 1.0f, 0.92f * A);
	Graphics()->DrawRect(X, Y - 3.0f, W, 6.0f, Snow, IGraphics::CORNER_ALL, 3.0f);
	Graphics()->DrawRect(X + W * 0.18f, Y - 5.0f, W * 0.28f, 5.0f, Snow,
		IGraphics::CORNER_ALL, 2.5f);
}

void CZZNewYearMenu::DrawSwitch(float X, float Y, float W, float H, float On, float Hover)
{
	const float A = m_Ctx.m_Alpha;
	const ColorRGBA Off(0.20f, 0.21f, 0.25f, 1.0f);
	const ColorRGBA Track(
		Off.r + (m_Ctx.m_Accent.r - Off.r) * On,
		Off.g + (m_Ctx.m_Accent.g - Off.g) * On,
		Off.b + (m_Ctx.m_Accent.b - Off.b) * On, 1.0f);
	if(On > 0.02f)
		Graphics()->DrawRect(X - 3.0f, Y - 3.0f, W + 6.0f, H + 6.0f,
			m_Ctx.m_Accent.WithAlpha(0.10f * On * A), IGraphics::CORNER_ALL, (H + 6.0f) * 0.5f);
	Graphics()->DrawRect(X, Y, W, H, Track.WithAlpha(A), IGraphics::CORNER_ALL, H * 0.5f);
	const float Knob = H - 6.0f;
	const float KnobX = X + 3.0f + (W - Knob - 6.0f) * On;
	Graphics()->DrawRect(KnobX, Y + 3.0f, Knob, Knob,
		ColorRGBA(0.97f + Hover * 0.03f, 0.97f, 0.98f, A), IGraphics::CORNER_ALL, Knob * 0.5f);
	if(m_Ctx.m_Decor)
	{
		// Peppermint dot on the knob and a snow cap on the track.
		Graphics()->DrawRect(KnobX + Knob * 0.5f - 3.0f, Y + 3.0f + Knob * 0.5f - 3.0f, 6.0f, 6.0f,
			ColorRGBA(0.86f, 0.16f, 0.24f, 0.9f * A), IGraphics::CORNER_ALL, 3.0f);
		DrawSnowCap(X + 2.0f, Y + 1.0f, W - 4.0f);
	}
}

// ----------------------------------------------------------------------------
// Rows and sections
// ----------------------------------------------------------------------------

float CZZNewYearMenu::DrawToggleRow(float X, float Y, float W, const SRowData &Row)
{
	const float A = m_Ctx.m_Alpha;
	const float H = ROW_TOGGLE_H;
	const bool On = g_Config.*(Row.m_pValue) != 0;
	SAnim &Anim = AnimFor(Row.m_pValue, On);
	const bool Hovered = PointInRect(m_MousePos, X + 6.0f, Y, W - 12.0f, H - 4.0f);
	Anim.m_On = Approach(Anim.m_On, On ? 1.0f : 0.0f, m_Ctx.m_Delta, 16.0f);
	Anim.m_Hover = Approach(Anim.m_Hover, Hovered ? 1.0f : 0.0f, m_Ctx.m_Delta, 14.0f);

	Graphics()->DrawRect(X + 6.0f, Y, W - 12.0f, H - 4.0f,
		ColorRGBA(1.0f, 1.0f, 1.0f, (0.018f + Anim.m_Hover * 0.045f) * A),
		IGraphics::CORNER_ALL, 12.0f);
	DrawTextFit(X + 18.0f, Y + 8.0f, 15.0f, Row.m_pTitle, m_Ctx.m_Text.WithAlpha(A), W - 120.0f);
	if(Row.m_pSub)
		DrawTextFit(X + 18.0f, Y + 29.0f, 11.5f, Row.m_pSub, m_Ctx.m_Muted.WithAlpha(A), W - 120.0f);
	DrawSwitch(X + W - 18.0f - 46.0f, Y + (H - 4.0f - 24.0f) * 0.5f, 46.0f, 24.0f,
		Anim.m_On, Anim.m_Hover);

	SHit Hit;
	Hit.m_X = X + 6.0f;
	Hit.m_Y = Y;
	Hit.m_W = W - 12.0f;
	Hit.m_H = H - 4.0f;
	Hit.m_Kind = HIT_TOGGLE;
	Hit.m_pValue = Row.m_pValue;
	AddHit(Hit);
	return H;
}

float CZZNewYearMenu::DrawSliderRow(float X, float Y, float W, const SRowData &Row)
{
	const float A = m_Ctx.m_Alpha;
	const int Value = std::clamp(g_Config.*(Row.m_pValue), Row.m_Min, Row.m_Max);
	const float Range = (float)std::max(Row.m_Max - Row.m_Min, 1);
	const float Target = (float)(Value - Row.m_Min) / Range;
	SAnim &Anim = AnimFor(Row.m_pValue, true);
	const bool Dragging = m_pDragValue == Row.m_pValue;
	const float TrackX = X + 18.0f;
	const float TrackW = W - 36.0f;
	const float TrackY = Y + 35.0f;
	const bool Hovered = Dragging ||
			     PointInRect(m_MousePos, TrackX - 6.0f, TrackY - 12.0f, TrackW + 12.0f, 30.0f);
	Anim.m_Hover = Approach(Anim.m_Hover, Hovered ? 1.0f : 0.0f, m_Ctx.m_Delta, 14.0f);
	Anim.m_On = Dragging ? Target : Approach(Anim.m_On, Target, m_Ctx.m_Delta, 22.0f);

	DrawTextFit(X + 18.0f, Y + 6.0f, 14.0f, Row.m_pTitle, m_Ctx.m_Text.WithAlpha(A), W - 140.0f);
	char aValue[48];
	str_format(aValue, sizeof(aValue), "%d%s", Value, Row.m_pSuffix ? Row.m_pSuffix : "");
	DrawText(X + W - 18.0f, Y + 6.0f, 14.0f, aValue, m_Ctx.m_Muted.WithAlpha(A), true);

	const float TrackH = 6.0f;
	Graphics()->DrawRect(TrackX, TrackY, TrackW, TrackH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * A),
		IGraphics::CORNER_ALL, TrackH * 0.5f);
	const float FillW = std::max(TrackH, TrackW * Anim.m_On);
	Graphics()->DrawRect(TrackX, TrackY, FillW, TrackH, m_Ctx.m_Accent.WithAlpha(A),
		IGraphics::CORNER_ALL, TrackH * 0.5f);
	if(m_Ctx.m_Decor)
	{
		// Candy-cane stripes on the filled part.
		for(float StripeX = TrackX + 4.0f; StripeX < TrackX + FillW - 6.0f; StripeX += 12.0f)
			Graphics()->DrawRect(StripeX, TrackY, 5.0f, TrackH,
				ColorRGBA(1.0f, 1.0f, 1.0f, 0.88f * A), IGraphics::CORNER_NONE, 0.0f);
	}
	const float Knob = 14.0f + Anim.m_Hover * 3.0f;
	const float KnobY = TrackY + TrackH * 0.5f;
	Graphics()->DrawRect(TrackX + FillW - Knob * 0.5f, KnobY - Knob * 0.5f, Knob, Knob,
		m_Ctx.m_Accent.WithAlpha(0.22f * A), IGraphics::CORNER_ALL, Knob * 0.5f);
	const float Core = Knob - 6.0f;
	Graphics()->DrawRect(TrackX + FillW - Core * 0.5f, KnobY - Core * 0.5f, Core, Core,
		ColorRGBA(0.97f, 0.97f, 0.98f, A), IGraphics::CORNER_ALL, Core * 0.5f);

	SHit Hit;
	Hit.m_X = TrackX - 8.0f;
	Hit.m_Y = TrackY - 14.0f;
	Hit.m_W = TrackW + 16.0f;
	Hit.m_H = 34.0f;
	Hit.m_Kind = HIT_SLIDER;
	Hit.m_pValue = Row.m_pValue;
	Hit.m_Min = Row.m_Min;
	Hit.m_Max = Row.m_Max;
	Hit.m_TrackX = TrackX;
	Hit.m_TrackW = TrackW;
	AddHit(Hit);
	return ROW_SLIDER_H;
}

float CZZNewYearMenu::DrawNoteRow(float X, float Y, float W, const SRowData &Row)
{
	DrawWrapped(X + 18.0f, Y + 6.0f, 11.5f, Row.m_pSub,
		m_Ctx.m_Muted.WithAlpha(m_Ctx.m_Alpha * 0.92f), W - 36.0f, 5);
	return ROW_NOTE_H;
}

float CZZNewYearMenu::SectionHeight(const SSectionData &Section) const
{
	float Height = SECTION_HEAD_H + SECTION_PAD;
	for(int i = 0; i < Section.m_NumRows; ++i)
		Height += RowHeight(Section.m_pRows[i].m_Type);
	return Height;
}

float CZZNewYearMenu::DrawSection(float X, float Y, float W, const SSectionData &Section)
{
	const float A = m_Ctx.m_Alpha;
	const float Height = SectionHeight(Section);
	Graphics()->DrawRect(X, Y, W, Height, ColorRGBA(0.075f, 0.078f, 0.10f, 0.92f * A),
		IGraphics::CORNER_ALL, 16.0f);
	Graphics()->DrawRect(X, Y, W, Height, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f * A),
		IGraphics::CORNER_ALL, 16.0f);
	// Small red "leaf" marker before the title, like on the reference.
	Graphics()->DrawRect(X + 18.0f, Y + 15.0f, 7.0f, 9.0f, m_Ctx.m_Accent.WithAlpha(A),
		IGraphics::CORNER_TL | IGraphics::CORNER_BR, 4.0f);
	DrawText(X + 32.0f, Y + 12.0f, 12.5f, Section.m_pTitle, m_Ctx.m_Muted.WithAlpha(A));
	float RowY = Y + SECTION_HEAD_H;
	for(int i = 0; i < Section.m_NumRows; ++i)
	{
		const SRowData &Row = Section.m_pRows[i];
		if(Row.m_Type == ROW_TOGGLE)
			RowY += DrawToggleRow(X, RowY, W, Row);
		else if(Row.m_Type == ROW_SLIDER)
			RowY += DrawSliderRow(X, RowY, W, Row);
		else
			RowY += DrawNoteRow(X, RowY, W, Row);
	}
	return Height;
}

// ----------------------------------------------------------------------------
// Header, sidebar, search
// ----------------------------------------------------------------------------

void CZZNewYearMenu::DrawHeader(float X, float Y, float W)
{
	const float A = m_Ctx.m_Alpha;
	const float H = 84.0f;
	Graphics()->DrawRect(X, Y, W, H, ColorRGBA(0.075f, 0.078f, 0.10f, 0.92f * A),
		IGraphics::CORNER_ALL, 18.0f);
	Graphics()->DrawRect(X, Y, W, H, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f * A),
		IGraphics::CORNER_ALL, 18.0f);

	Graphics()->DrawRect(X + 14.0f, Y + 14.0f, 56.0f, 56.0f,
		m_Ctx.m_Accent.WithAlpha(0.18f * A), IGraphics::CORNER_ALL, 16.0f);
	Graphics()->DrawRect(X + 16.0f, Y + 16.0f, 52.0f, 52.0f, m_Ctx.m_Accent.WithAlpha(0.92f * A),
		IGraphics::CORNER_ALL, 14.0f);
	DrawIcon(X + 16.0f, Y + 16.0f, 52.0f, 52.0f, 22.0f, FontIcon::HEART, ColorRGBA(1.0f, 1.0f, 1.0f, A));

	const float PillW = 118.0f;
	const float PillH = 44.0f;
	const float TextW = W - 86.0f - PillW - 48.0f;
	DrawText(X + 86.0f, Y + 11.0f, 26.0f, PAGE_TITLE, m_Ctx.m_Text.WithAlpha(A));
	DrawWrapped(X + 86.0f, Y + 44.0f, 12.5f, PAGE_DESC, m_Ctx.m_Muted.WithAlpha(A), TextW, 2);

	int CConfig::*pMaster = &CConfig::m_ClZzAutoUnfreeze;
	const bool On = g_Config.*pMaster != 0;
	SAnim &Anim = AnimFor(pMaster, On);
	const float PX = X + W - 20.0f - PillW;
	const float PY = Y + (H - PillH) * 0.5f;
	const bool Hovered = PointInRect(m_MousePos, PX, PY, PillW, PillH);
	Anim.m_On = Approach(Anim.m_On, On ? 1.0f : 0.0f, m_Ctx.m_Delta, 14.0f);
	Anim.m_Hover = Approach(Anim.m_Hover, Hovered ? 1.0f : 0.0f, m_Ctx.m_Delta, 14.0f);
	const ColorRGBA OffColor(0.13f, 0.135f, 0.16f, 1.0f);
	const ColorRGBA OnColor(m_Ctx.m_Accent.r * 0.65f, m_Ctx.m_Accent.g * 0.65f,
		m_Ctx.m_Accent.b * 0.65f, 1.0f);
	const ColorRGBA Track(
		OffColor.r + (OnColor.r - OffColor.r) * Anim.m_On,
		OffColor.g + (OnColor.g - OffColor.g) * Anim.m_On,
		OffColor.b + (OnColor.b - OffColor.b) * Anim.m_On, 1.0f);
	Graphics()->DrawRect(PX - 2.0f, PY - 2.0f, PillW + 4.0f, PillH + 4.0f,
		m_Ctx.m_Accent.WithAlpha((0.05f + Anim.m_On * 0.12f + Anim.m_Hover * 0.05f) * A),
		IGraphics::CORNER_ALL, (PillH + 4.0f) * 0.5f);
	Graphics()->DrawRect(PX, PY, PillW, PillH, Track.WithAlpha(A), IGraphics::CORNER_ALL, PillH * 0.5f);
	const float Knob = PillH - 8.0f;
	const float KnobX = PX + 4.0f + (PillW - Knob - 8.0f) * Anim.m_On;
	Graphics()->DrawRect(KnobX, PY + 4.0f, Knob, Knob, ColorRGBA(0.98f, 0.98f, 1.0f, A),
		IGraphics::CORNER_ALL, Knob * 0.5f);
	const char *pState = On ? "ВКЛ" : "ВЫКЛ";
	const float StateW = TextRender()->TextWidth(14.0f, pState);
	const float LabelX = On ? PX + 18.0f : PX + PillW - 18.0f - StateW;
	DrawText(LabelX, PY + (PillH - 14.0f) * 0.5f - 1.0f, 14.0f, pState,
		(On ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : m_Ctx.m_Muted).WithAlpha(A));
	DrawSnowCap(PX + 6.0f, PY + 1.0f, PillW - 12.0f);

	SHit Hit;
	Hit.m_X = PX;
	Hit.m_Y = PY;
	Hit.m_W = PillW;
	Hit.m_H = PillH;
	Hit.m_Kind = HIT_TOGGLE;
	Hit.m_pValue = pMaster;
	AddHit(Hit);
}

void CZZNewYearMenu::DrawSidebar(float X, float Y)
{
	const float A = m_Ctx.m_Alpha;
	const float Button = 52.0f;
	const float Pad = 8.0f;
	const float Gap = 10.0f;
	const float Width = Button + Pad * 2.0f;
	const float Height = Pad * 2.0f + Button * 2.0f + Gap;
	Graphics()->DrawRect(X, Y, Width, Height, ColorRGBA(0.055f, 0.057f, 0.075f, 0.92f * A),
		IGraphics::CORNER_ALL, Width * 0.5f);
	Graphics()->DrawRect(X, Y, Width, Height, ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f * A),
		IGraphics::CORNER_ALL, Width * 0.5f);

	// Active module button with a Santa hat.
	const float BX = X + Pad;
	const float BY = Y + Pad;
	Graphics()->DrawRect(BX - 2.0f, BY - 2.0f, Button + 4.0f, Button + 4.0f,
		m_Ctx.m_Accent.WithAlpha(0.18f * A), IGraphics::CORNER_ALL, (Button + 4.0f) * 0.5f);
	Graphics()->DrawRect(BX, BY, Button, Button, m_Ctx.m_Accent.WithAlpha(0.95f * A),
		IGraphics::CORNER_ALL, Button * 0.5f);
	DrawIcon(BX, BY, Button, Button, 20.0f, FontIcon::HEART, ColorRGBA(1.0f, 1.0f, 1.0f, A));
	if(m_Ctx.m_Decor)
	{
		Graphics()->DrawRect(BX + 4.0f, BY - 20.0f, 26.0f, 20.0f, ColorRGBA(0.85f, 0.12f, 0.20f, A),
			IGraphics::CORNER_T, 10.0f);
		Graphics()->DrawRect(BX + 1.0f, BY - 5.0f, 34.0f, 9.0f, ColorRGBA(0.97f, 0.98f, 1.0f, A),
			IGraphics::CORNER_ALL, 4.5f);
		Graphics()->DrawRect(BX + 25.0f, BY - 26.0f, 11.0f, 11.0f, ColorRGBA(0.97f, 0.98f, 1.0f, A),
			IGraphics::CORNER_ALL, 5.5f);
	}

	// Decoration switch (gear).
	const float GY = BY + Button + Gap;
	const bool Hovered = PointInRect(m_MousePos, BX, GY, Button, Button);
	Graphics()->DrawRect(BX, GY, Button, Button,
		ColorRGBA(1.0f, 1.0f, 1.0f, (0.03f + (Hovered ? 0.07f : 0.0f)) * A),
		IGraphics::CORNER_ALL, Button * 0.5f);
	DrawIcon(BX, GY, Button, Button, 20.0f, FontIcon::GEAR,
		(m_Ctx.m_Decor ? m_Ctx.m_Text : m_Ctx.m_Muted).WithAlpha(A));
	SHit Hit;
	Hit.m_X = BX;
	Hit.m_Y = GY;
	Hit.m_W = Button;
	Hit.m_H = Button;
	Hit.m_Kind = HIT_TOGGLE;
	Hit.m_pValue = &CConfig::m_ClZzNewYearMenuDecor;
	AddHit(Hit);
}

void CZZNewYearMenu::DrawSearchBar(float X, float Y, float W)
{
	const float A = m_Ctx.m_Alpha;
	const float H = 42.0f;
	const bool Hovered = PointInRect(m_MousePos, X, Y, W, H);
	Graphics()->DrawRect(X - 2.0f, Y - 2.0f, W + 4.0f, H + 4.0f,
		m_Ctx.m_Accent.WithAlpha((m_SearchFocused ? 0.30f : 0.07f) * A),
		IGraphics::CORNER_ALL, (H + 4.0f) * 0.5f);
	Graphics()->DrawRect(X, Y, W, H, ColorRGBA(0.055f, 0.057f, 0.075f, 0.95f * A),
		IGraphics::CORNER_ALL, H * 0.5f);
	Graphics()->DrawRect(X, Y, W, H, ColorRGBA(1.0f, 1.0f, 1.0f, (0.03f + (Hovered ? 0.03f : 0.0f)) * A),
		IGraphics::CORNER_ALL, H * 0.5f);
	DrawIcon(X + 10.0f, Y, 32.0f, H, 14.0f, FontIcon::MAGNIFYING_GLASS, m_Ctx.m_Muted.WithAlpha(A));
	const float TextX = X + 46.0f;
	const float TextMax = W - 46.0f - 20.0f;
	const float TextY = Y + (H - 14.0f) * 0.5f - 1.0f;
	if(m_aSearch[0] == '\0')
	{
		if(!m_SearchFocused)
			DrawText(TextX, TextY, 14.0f, "Поиск по функциям...", m_Ctx.m_Muted.WithAlpha(A));
	}
	else
		DrawTextFit(TextX, TextY, 14.0f, m_aSearch, m_Ctx.m_Text.WithAlpha(A), TextMax);
	if(m_SearchFocused && std::fmod(m_Ctx.m_Time, 1.0f) < 0.55f)
	{
		const float Typed = m_aSearch[0] ? std::min(TextRender()->TextWidth(14.0f, m_aSearch), TextMax) : 0.0f;
		Graphics()->DrawRect(TextX + Typed + 1.0f, Y + 11.0f, 1.5f, H - 22.0f, m_Ctx.m_Text.WithAlpha(A),
			IGraphics::CORNER_NONE, 0.0f);
	}
	DrawSnowCap(X + W * 0.30f, Y + 1.0f, W * 0.40f);

	SHit Hit;
	Hit.m_X = X;
	Hit.m_Y = Y;
	Hit.m_W = W;
	Hit.m_H = H;
	Hit.m_Kind = HIT_SEARCH;
	AddHit(Hit);
}

void CZZNewYearMenu::DrawSearchResults(float X, float Y, float W)
{
	constexpr int MAX_RESULTS = 8;
	SRowData aResults[MAX_RESULTS];
	int NumResults = 0;
	for(const SSectionData &Section : s_aSections)
	{
		for(int i = 0; i < Section.m_NumRows && NumResults < MAX_RESULTS; ++i)
		{
			const SRowData &Row = Section.m_pRows[i];
			if(Row.m_Type == ROW_NOTE || !str_find_nocase(Row.m_pTitle, m_aSearch))
				continue;
			aResults[NumResults++] = Row;
		}
	}
	if(NumResults == 0)
	{
		DrawText(X + 18.0f, Y + 12.0f, 15.0f, "Ничего не найдено", m_Ctx.m_Muted.WithAlpha(m_Ctx.m_Alpha));
		return;
	}
	const SSectionData Results = {"РЕЗУЛЬТАТЫ ПОИСКА", 0, aResults, NumResults};
	DrawSection(X, Y, (W - 16.0f) * 0.54f, Results);
}

// ----------------------------------------------------------------------------
// Decorations
// ----------------------------------------------------------------------------

void CZZNewYearMenu::DrawSnow(float ScreenW, float ScreenH)
{
	const float A = m_Ctx.m_Alpha;
	for(int i = 0; i < 46; ++i)
	{
		const float SeedX = Hash01((uint32_t)i * 17U + 3U);
		const float SeedY = Hash01((uint32_t)i * 29U + 11U);
		const float SeedS = Hash01((uint32_t)i * 43U + 7U);
		const float Drift = std::sin(m_Ctx.m_Time * (0.4f + SeedY * 0.5f) + SeedX * 12.0f) *
				    (10.0f + SeedY * 16.0f);
		const float X = SeedX * ScreenW + Drift;
		const float Y = std::fmod(SeedY * ScreenH + m_Ctx.m_Time * (14.0f + SeedS * 26.0f),
					  ScreenH + 24.0f) - 12.0f;
		const float Size = 1.6f + SeedS * 2.8f;
		Graphics()->DrawRect(X, Y, Size, Size, ColorRGBA(1.0f, 1.0f, 1.0f, (0.35f + SeedS * 0.4f) * A),
			IGraphics::CORNER_ALL, Size * 0.5f);
	}
}

void CZZNewYearMenu::DrawPineFrame(float X, float Y, float W, float H)
{
	const float A = m_Ctx.m_Alpha;
	const float Band = 15.0f;

	// Needles are batched: three shades x three angles, drawn in nine calls.
	constexpr int MAX_NEEDLES = 220;
	static IGraphics::CQuadItem s_aNeedles[3][3][MAX_NEEDLES];
	int aCount[3][3] = {};
	auto AddNeedle = [&](float CX, float CY, bool Horizontal, uint32_t Seed) {
		const int Shade = (int)(Hash01(Seed * 3U + 1U) * 2.99f);
		const int Angle = (int)(Hash01(Seed * 5U + 2U) * 2.99f);
		const float Length = 15.0f + Hash01(Seed * 7U + 3U) * 9.0f;
		const float Thick = 4.0f + Hash01(Seed * 11U + 4U) * 2.0f;
		const float Jitter = (Hash01(Seed * 13U + 5U) - 0.5f) * Band * 0.7f;
		if(aCount[Shade][Angle] >= MAX_NEEDLES)
			return;
		const float PX = Horizontal ? CX : CX + Jitter;
		const float PY = Horizontal ? CY + Jitter : CY;
		const float W2 = Horizontal ? Length : Thick;
		const float H2 = Horizontal ? Thick : Length;
		s_aNeedles[Shade][Angle][aCount[Shade][Angle]++] =
			IGraphics::CQuadItem(PX - W2 * 0.5f, PY - H2 * 0.5f, W2, H2);
	};
	uint32_t Seed = 1;
	for(float PX = X - 6.0f; PX < X + W + 8.0f; PX += 5.0f)
	{
		AddNeedle(PX, Y, true, Seed++);
		AddNeedle(PX, Y + H, true, Seed++);
	}
	for(float PY = Y + 4.0f; PY < Y + H - 2.0f; PY += 5.0f)
	{
		AddNeedle(X, PY, false, Seed++);
		AddNeedle(X + W, PY, false, Seed++);
	}
	const ColorRGBA aShades[3] = {
		ColorRGBA(0.05f, 0.25f, 0.11f, 1.0f),
		ColorRGBA(0.08f, 0.33f, 0.15f, 1.0f),
		ColorRGBA(0.13f, 0.44f, 0.20f, 1.0f),
	};
	const float aAngles[3] = {-0.45f, 0.0f, 0.45f};
	Graphics()->TextureClear();
	for(int Shade = 0; Shade < 3; ++Shade)
	{
		for(int Angle = 0; Angle < 3; ++Angle)
		{
			if(aCount[Shade][Angle] == 0)
				continue;
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetRotation(aAngles[Angle]);
			Graphics()->SetColor(aShades[Shade].WithAlpha(A));
			Graphics()->QuadsDrawTL(s_aNeedles[Shade][Angle], aCount[Shade][Angle]);
			Graphics()->QuadsEnd();
		}
	}
	Graphics()->QuadsSetRotation(0.0f);

	// Snow lying on the top branch.
	for(float PX = X + 10.0f; PX < X + W - 20.0f; PX += 38.0f)
	{
		const float Width = 16.0f + Hash01((uint32_t)PX) * 14.0f;
		Graphics()->DrawRect(PX, Y - Band * 0.5f - 1.0f, Width, 5.0f,
			ColorRGBA(0.97f, 0.98f, 1.0f, 0.88f * A), IGraphics::CORNER_ALL, 2.5f);
	}

	// Light bulbs hanging inside the frame.
	static const ColorRGBA s_aBulbs[] = {
		ColorRGBA(1.0f, 0.22f, 0.25f, 1.0f),
		ColorRGBA(1.0f, 0.78f, 0.20f, 1.0f),
		ColorRGBA(0.25f, 0.90f, 0.40f, 1.0f),
		ColorRGBA(0.30f, 0.62f, 1.0f, 1.0f),
		ColorRGBA(1.0f, 0.52f, 0.18f, 1.0f),
	};
	int Index = 0;
	auto Bulb = [&](float BX, float BY) {
		const ColorRGBA Color = s_aBulbs[Index % (int)std::size(s_aBulbs)];
		const float Twinkle = 0.62f + 0.38f * std::sin(m_Ctx.m_Time * 2.4f + Index * 1.7f);
		Graphics()->DrawRect(BX - 7.0f, BY - 7.0f, 14.0f, 14.0f, Color.WithAlpha(0.12f * Twinkle * A),
			IGraphics::CORNER_ALL, 7.0f);
		Graphics()->DrawRect(BX - 3.0f, BY - 3.5f, 6.0f, 8.0f, Color.WithAlpha(0.95f * Twinkle * A),
			IGraphics::CORNER_ALL, 3.0f);
		++Index;
	};
	for(float PX = X + 24.0f; PX < X + W - 14.0f; PX += 44.0f)
	{
		Bulb(PX, Y + 13.0f);
		Bulb(PX + 22.0f, Y + H - 13.0f);
	}
	for(float PY = Y + 34.0f; PY < Y + H - 20.0f; PY += 44.0f)
	{
		Bulb(X + 13.0f, PY);
		Bulb(X + W - 13.0f, PY + 22.0f);
	}

	// Ornaments along the frame and large ones in the corners.
	static const ColorRGBA s_aBalls[] = {
		ColorRGBA(0.86f, 0.14f, 0.22f, 1.0f),
		ColorRGBA(0.92f, 0.74f, 0.26f, 1.0f),
		ColorRGBA(0.80f, 0.84f, 0.90f, 1.0f),
		ColorRGBA(0.18f, 0.62f, 0.32f, 1.0f),
	};
	int BallIndex = 0;
	for(float PX = X + 70.0f; PX < X + W - 40.0f; PX += 130.0f)
	{
		DrawBall(PX, Y + 4.0f, 6.5f, s_aBalls[BallIndex++ % (int)std::size(s_aBalls)]);
		DrawBall(PX + 65.0f, Y + H - 2.0f, 6.5f, s_aBalls[BallIndex++ % (int)std::size(s_aBalls)]);
	}
	for(float PY = Y + 110.0f; PY < Y + H - 60.0f; PY += 150.0f)
	{
		DrawBall(X + 2.0f, PY, 6.5f, s_aBalls[BallIndex++ % (int)std::size(s_aBalls)]);
		DrawBall(X + W - 2.0f, PY + 50.0f, 6.5f, s_aBalls[BallIndex++ % (int)std::size(s_aBalls)]);
	}
	DrawBall(X + 2.0f, Y + 4.0f, 11.0f, s_aBalls[0]);
	DrawBall(X + W - 2.0f, Y + 4.0f, 11.0f, s_aBalls[3]);
	DrawBall(X + 2.0f, Y + H - 2.0f, 11.0f, s_aBalls[1]);
	DrawBall(X + W - 2.0f, Y + H - 2.0f, 11.0f, s_aBalls[0]);
}

// ----------------------------------------------------------------------------
// Main render
// ----------------------------------------------------------------------------

void CZZNewYearMenu::OnRender()
{
	if(g_Config.m_ClZzNewYearMenu == 0)
	{
		Cleanup();
		return;
	}

	const bool JustOpened = !m_WasOpen;
	m_WasOpen = true;
	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.05f);
	m_Anim = Approach(m_Anim, 1.0f, Delta, 9.0f);
	if(m_Anim > 0.999f)
		m_Anim = 1.0f;
	const float Eased = m_Anim * m_Anim * (3.0f - 2.0f * m_Anim);

	const int PhysicalW = Graphics()->ScreenWidth();
	const int PhysicalH = Graphics()->ScreenHeight();
	m_UiScale = std::clamp(std::min(PhysicalW / 1700.0f, PhysicalH / 960.0f), 0.60f, 1.80f);
	const float ScreenW = PhysicalW / m_UiScale;
	const float ScreenH = PhysicalH / m_UiScale;

	if(JustOpened)
	{
		m_OpenedOverMenu = GameClient()->m_Menus.IsActive();
		if(m_OpenedOverMenu)
		{
			const CUIRect *pUiScreen = Ui()->Screen();
			if(pUiScreen->w > 1.0f && pUiScreen->h > 1.0f)
				m_MousePos = (Ui()->MousePos() - vec2(pUiScreen->x, pUiScreen->y)) /
					     vec2(pUiScreen->w, pUiScreen->h) * vec2(ScreenW, ScreenH);
		}
		else
			Input()->MouseModeAbsolute();
	}
	UpdateMousePosition();
	if(m_pDragValue)
		UpdateDraggedSlider();

	m_Ctx.m_Decor = g_Config.m_ClZzNewYearMenuDecor != 0;
	m_Ctx.m_Delta = Delta;
	m_Ctx.m_Time = LocalTime();
	m_Ctx.m_Alpha = Eased;

	Graphics()->MapScreen(0.0f, 0.0f, ScreenW, ScreenH);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, ScreenW, ScreenH, ColorRGBA(0.0f, 0.0f, 0.02f, 0.50f * Eased),
		IGraphics::CORNER_NONE, 0.0f);

	m_NumHits = 0;
	const float WinW = std::min(1100.0f, ScreenW - 240.0f);
	const float WinH = std::min(590.0f, ScreenH - 200.0f);
	const float X0 = (ScreenW - (WinW + 80.0f)) * 0.5f + 80.0f;
	const float Y0 = (ScreenH - WinH) * 0.5f - 22.0f + (1.0f - Eased) * 18.0f;

	Graphics()->DrawRect(X0 - 2.0f, Y0 - 2.0f, WinW + 4.0f, WinH + 4.0f,
		m_Ctx.m_Accent.WithAlpha(0.10f * Eased), IGraphics::CORNER_ALL, 28.0f);
	Graphics()->DrawRect(X0, Y0, WinW, WinH, ColorRGBA(0.028f, 0.030f, 0.042f, 0.90f * Eased),
		IGraphics::CORNER_ALL, 26.0f);
	Graphics()->DrawRect(X0, Y0, WinW, WinH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.025f * Eased),
		IGraphics::CORNER_ALL, 26.0f);
	if(m_Ctx.m_Decor)
		DrawSnow(ScreenW, ScreenH);

	DrawSidebar(X0 - 84.0f, Y0 + 34.0f);
	DrawHeader(X0 + 22.0f, Y0 + 22.0f, WinW - 44.0f);

	const bool On = g_Config.m_ClZzAutoUnfreeze != 0;
	const float StatusY = Y0 + 22.0f + 84.0f + 14.0f;
	Graphics()->DrawRect(X0 + 28.0f, StatusY + 4.0f, 7.0f, 7.0f,
		(On ? m_Ctx.m_Green : m_Ctx.m_Muted).WithAlpha(0.9f * Eased), IGraphics::CORNER_ALL, 3.5f);
	DrawText(X0 + 42.0f, StatusY, 12.5f, On ? "Включено" : "Выключено", m_Ctx.m_Muted.WithAlpha(Eased));

	const float ContentY = Y0 + 22.0f + 84.0f + 38.0f;
	const float ContentW = WinW - 44.0f;
	const float Gap = 16.0f;
	const float LeftW = (ContentW - Gap) * 0.54f;
	const float RightW = ContentW - Gap - LeftW;
	if(m_aSearch[0] != '\0')
		DrawSearchResults(X0 + 22.0f, ContentY, ContentW);
	else
	{
		for(int Column = 0; Column < 2; ++Column)
		{
			const float ColX = X0 + 22.0f + (Column == 0 ? 0.0f : LeftW + Gap);
			const float ColW = Column == 0 ? LeftW : RightW;
			float ColY = ContentY;
			for(const SSectionData &Section : s_aSections)
			{
				if(Section.m_Column == Column)
					ColY += DrawSection(ColX, ColY, ColW, Section) + SECTION_GAP;
			}
		}
	}

	const float FooterY = Y0 + WinH + 26.0f;
	DrawSearchBar(X0 + (WinW - 360.0f) * 0.5f, FooterY, 360.0f);
	DrawText(X0 + WinW - 6.0f, FooterY + 14.0f, 12.0f, "Esc - закрыть меню",
		m_Ctx.m_Muted.WithAlpha(0.8f * Eased), true);

	if(m_Ctx.m_Decor)
		DrawPineFrame(X0, Y0, WinW, WinH);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
