#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr UINT_PTR kFrameTimerId = 1;
constexpr UINT kFrameTimerMs = 16;
constexpr float kPi = 3.1415926535f;

// CAMBIO MANUAL 1:
// Estos valores ajustan la "sensacion" del disparo para que se note mas el
// efecto del viento y la curva del proyectil.
constexpr float kGravity = 430.0f;
constexpr float kVelocityScale = 6.8f;
constexpr float kWindAccelerationScale = 14.5f;
constexpr float kPreviewStep = 0.08f;

constexpr int kRoundsToWin = 2;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Star {
    int x = 0;
    int y = 0;
    int radius = 1;
};

struct Cloud {
    float x = 0.0f;
    float y = 0.0f;
    float size = 1.0f;
    float speed = 1.0f;
};

struct Building {
    RECT bounds{};
    COLORREF bodyColor = RGB(30, 30, 30);
    COLORREF trimColor = RGB(0, 0, 0);
    int windowCols = 0;
    int windowRows = 0;
    std::vector<bool> litWindows;
};

struct Player {
    int id = 0;
    std::string label;
    COLORREF accentColor = RGB(255, 255, 255);
    int buildingIndex = 0;
    Vec2 anchor{};
    RECT hitBox{};
};

struct Projectile {
    bool active = false;
    int ownerIndex = 0;
    Vec2 position{};
    Vec2 velocity{};
    std::vector<Vec2> trail;
};

struct Explosion {
    bool active = false;
    Vec2 position{};
    float radius = 0.0f;
    float maxRadius = 0.0f;
};

enum class Scene {
    Menu,
    Playing,
    RoundOver,
    MatchOver
};

enum class InputPhase {
    Angle,
    Speed
};

enum class PostShotOutcome {
    None,
    NextTurn,
    WinForPlayer1,
    WinForPlayer2
};

struct InputState {
    InputPhase phase = InputPhase::Angle;
    std::string buffer;
    float angle = 45.0f;
    float speed = 60.0f;
};

struct GameState {
    Scene scene = Scene::Menu;
    InputState input{};
    std::vector<Building> buildings;
    std::array<Player, 2> players{};
    std::vector<Star> stars;
    std::vector<Cloud> clouds;
    Projectile projectile{};
    Explosion explosion{};
    std::array<int, 2> scores{0, 0};
    std::array<float, 2> lastAngle{45.0f, 45.0f};
    std::array<float, 2> lastSpeed{60.0f, 60.0f};
    std::array<bool, 2> hasLastShot{false, false};
    int currentPlayer = 0;
    int roundNumber = 1;
    int wind = 0;
    bool showHelp = true;
    bool allowPreview = true;
    std::string status = "Presiona Enter para comenzar.";
    std::string roundBanner = "GORILLAS RELOADED";
    PostShotOutcome postShotOutcome = PostShotOutcome::None;
    float postShotDelay = 0.0f;
    float animationTime = 0.0f;
    std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();
};

HWND g_window = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_subtitleFont = nullptr;
HFONT g_uiFont = nullptr;
HFONT g_smallFont = nullptr;
HFONT g_inputFont = nullptr;

std::mt19937 g_rng(static_cast<unsigned int>(
    std::chrono::high_resolution_clock::now().time_since_epoch().count()));
GameState g_state;

float degToRad(float degrees) {
    return degrees * kPi / 180.0f;
}

int randomInt(int minValue, int maxValue) {
    std::uniform_int_distribution<int> distribution(minValue, maxValue);
    return distribution(g_rng);
}

float randomFloat(float minValue, float maxValue) {
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(g_rng);
}

RECT makeRect(int left, int top, int right, int bottom) {
    RECT rect{};
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    return rect;
}

RECT centeredRect(int centerX, int centerY, int width, int height) {
    return makeRect(centerX - width / 2, centerY - height / 2,
                    centerX + width / 2, centerY + height / 2);
}

bool pointInRect(const Vec2& point, const RECT& rect) {
    return point.x >= rect.left && point.x <= rect.right &&
           point.y >= rect.top && point.y <= rect.bottom;
}

COLORREF blendColor(COLORREF a, COLORREF b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const int red = static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    const int green = static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    const int blue = static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(red, green, blue);
}

std::string formatNumber(float value, int decimals = 0) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(decimals) << value;
    return output.str();
}

std::string currentPlayerName(const GameState& state) {
    return state.players[state.currentPlayer].label;
}

Vec2 projectileSpawnPoint(const Player& player) {
    const float offset = player.id == 0 ? 30.0f : -30.0f;
    return Vec2{player.anchor.x + offset, player.anchor.y - 56.0f};
}

void fillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void drawCircle(HDC hdc, int centerX, int centerY, int radius,
                COLORREF fillColor, COLORREF outlineColor, int penWidth = 1) {
    HPEN pen = CreatePen(PS_SOLID, penWidth, outlineColor);
    HBRUSH brush = CreateSolidBrush(fillColor);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawRoundedPanel(HDC hdc, const RECT& rect, COLORREF fillColor, COLORREF outlineColor) {
    HPEN pen = CreatePen(PS_SOLID, 2, outlineColor);
    HBRUSH brush = CreateSolidBrush(fillColor);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 22, 22);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void drawTextBlock(HDC hdc, const RECT& rect, const std::string& text,
                   HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    RECT copy = rect;
    DrawTextA(hdc, text.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void updatePlayerHitBox(Player& player) {
    player.hitBox = makeRect(static_cast<int>(player.anchor.x - 24.0f),
                             static_cast<int>(player.anchor.y - 72.0f),
                             static_cast<int>(player.anchor.x + 24.0f),
                             static_cast<int>(player.anchor.y - 8.0f));
}

void createFonts() {
    g_titleFont = CreateFontA(54, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, VARIABLE_PITCH, "Bahnschrift");
    g_subtitleFont = CreateFontA(28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Trebuchet MS");
    g_uiFont = CreateFontA(23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
    g_smallFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
    g_inputFont = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
}

void destroyFonts() {
    DeleteObject(g_titleFont);
    DeleteObject(g_subtitleFont);
    DeleteObject(g_uiFont);
    DeleteObject(g_smallFont);
    DeleteObject(g_inputFont);
}

void seedSkyDecor(GameState& state) {
    state.stars.clear();
    state.clouds.clear();

    for (int i = 0; i < 90; ++i) {
        state.stars.push_back(Star{
            randomInt(0, kWindowWidth),
            randomInt(10, 260),
            randomInt(1, 2)
        });
    }

    for (int i = 0; i < 6; ++i) {
        state.clouds.push_back(Cloud{
            randomFloat(0.0f, static_cast<float>(kWindowWidth)),
            randomFloat(70.0f, 260.0f),
            randomFloat(0.8f, 1.6f),
            randomFloat(8.0f, 22.0f)
        });
    }
}

void resetInput(GameState& state) {
    state.input.phase = InputPhase::Angle;
    state.input.buffer.clear();
    state.input.angle = 45.0f;
    state.input.speed = 60.0f;
}

void setTurnPrompt(GameState& state) {
    state.status = currentPlayerName(state) + ": ingresa el angulo (10 a 85) y presiona Enter.";
}

void buildCity(GameState& state) {
    state.buildings.clear();

    // CAMBIO MANUAL 2:
    // Se cambiaron anchos y alturas para que la ciudad se vea mas variada
    // y tenga edificios altos y bajos como en un skyline real.
    const int baseLineY = kWindowHeight - 70;
    int x = 0;

    while (x < kWindowWidth) {
        const int width = randomInt(56, 96);
        const int height = randomInt(150, 390);
        const int right = std::min(x + width, kWindowWidth);

        Building building;
        building.bounds = makeRect(x, baseLineY - height, right, baseLineY);
        building.bodyColor = RGB(randomInt(28, 56), randomInt(36, 80), randomInt(72, 122));
        building.trimColor = blendColor(building.bodyColor, RGB(220, 210, 180), 0.30f);
        building.windowCols = std::max(2, (right - x - 14) / 15);
        building.windowRows = std::max(4, (height - 22) / 19);
        building.litWindows.resize(building.windowCols * building.windowRows);

        for (int i = 0; i < static_cast<int>(building.litWindows.size()); ++i) {
            building.litWindows[i] = randomInt(0, 100) < 38;
        }

        state.buildings.push_back(building);
        x = right;
    }
}

void placePlayers(GameState& state) {
    if (state.buildings.size() < 4) {
        return;
    }

    const int leftIndex = std::max(1, static_cast<int>(state.buildings.size() * 0.18f));
    const int rightIndex = std::min(static_cast<int>(state.buildings.size()) - 2,
                                    static_cast<int>(state.buildings.size() * 0.80f));

    state.players[0].id = 0;
    state.players[0].label = "Jugador 1";
    state.players[0].accentColor = RGB(255, 198, 80);
    state.players[0].buildingIndex = leftIndex;
    state.players[0].anchor = Vec2{
        (state.buildings[leftIndex].bounds.left + state.buildings[leftIndex].bounds.right) / 2.0f,
        static_cast<float>(state.buildings[leftIndex].bounds.top)
    };
    updatePlayerHitBox(state.players[0]);

    state.players[1].id = 1;
    state.players[1].label = "Jugador 2";
    state.players[1].accentColor = RGB(112, 224, 255);
    state.players[1].buildingIndex = rightIndex;
    state.players[1].anchor = Vec2{
        (state.buildings[rightIndex].bounds.left + state.buildings[rightIndex].bounds.right) / 2.0f,
        static_cast<float>(state.buildings[rightIndex].bounds.top)
    };
    updatePlayerHitBox(state.players[1]);
}

void generateWind(GameState& state) {
    state.wind = randomInt(-9, 9);
}

void prepareBackdrop(GameState& state) {
    buildCity(state);
    placePlayers(state);
    generateWind(state);
    state.projectile = Projectile{};
    state.explosion = Explosion{};
    state.postShotOutcome = PostShotOutcome::None;
    state.postShotDelay = 0.0f;
}

void startRound(GameState& state) {
    prepareBackdrop(state);
    state.scene = Scene::Playing;
    state.currentPlayer = (state.roundNumber + 1) % 2;
    resetInput(state);
    setTurnPrompt(state);
}

void startMatch(GameState& state) {
    state.scores = {0, 0};
    state.lastAngle = {45.0f, 45.0f};
    state.lastSpeed = {60.0f, 60.0f};
    state.hasLastShot = {false, false};
    state.roundNumber = 1;
    state.roundBanner.clear();
    startRound(state);
}

void backToMenu(GameState& state) {
    prepareBackdrop(state);
    state.scene = Scene::Menu;
    state.roundBanner = "GORILLAS RELOADED";
    state.status = "Presiona Enter para comenzar la partida.";
}

void advanceTurn(GameState& state) {
    state.currentPlayer = 1 - state.currentPlayer;
    resetInput(state);
    setTurnPrompt(state);
}

void completeRound(GameState& state, int winnerIndex) {
    ++state.scores[winnerIndex];
    const std::string winnerName = state.players[winnerIndex].label;

    // CAMBIO MANUAL 3:
    // La regla original de "una sola ronda" se cambio por una partida al
    // mejor de 3, usando un marcador persistente entre rondas.
    if (state.scores[winnerIndex] >= kRoundsToWin) {
        state.scene = Scene::MatchOver;
        state.roundBanner = winnerName + " gana la partida.";
        state.status = "Enter: nueva partida | M: menu | H: ayuda";
        return;
    }

    state.scene = Scene::RoundOver;
    state.roundBanner = winnerName + " gana la ronda " + formatNumber(static_cast<float>(state.roundNumber)) + ".";
    state.status = "Enter: siguiente ronda | R: reiniciar partida";
    ++state.roundNumber;
}

void launchProjectile(GameState& state, float angle, float speed) {
    Projectile projectile;
    projectile.active = true;
    projectile.ownerIndex = state.currentPlayer;
    projectile.position = projectileSpawnPoint(state.players[state.currentPlayer]);
    projectile.trail.push_back(projectile.position);

    const float angleRad = degToRad(angle);
    const float horizontalDirection = state.currentPlayer == 0 ? 1.0f : -1.0f;
    projectile.velocity.x = std::cos(angleRad) * speed * kVelocityScale * horizontalDirection;
    projectile.velocity.y = -std::sin(angleRad) * speed * kVelocityScale;

    state.projectile = projectile;
    state.input.buffer.clear();
    state.status = currentPlayerName(state) + " disparo con angulo " +
                   formatNumber(angle) + " y velocidad " + formatNumber(speed) + ".";
}

void triggerExplosion(GameState& state, const Vec2& position, float maxRadius, PostShotOutcome outcome) {
    state.projectile.active = false;
    state.explosion.active = true;
    state.explosion.position = position;
    state.explosion.radius = 4.0f;
    state.explosion.maxRadius = maxRadius;
    state.postShotOutcome = outcome;
}

void applyPostShotOutcome(GameState& state) {
    switch (state.postShotOutcome) {
        case PostShotOutcome::NextTurn:
            advanceTurn(state);
            break;
        case PostShotOutcome::WinForPlayer1:
            completeRound(state, 0);
            break;
        case PostShotOutcome::WinForPlayer2:
            completeRound(state, 1);
            break;
        case PostShotOutcome::None:
            break;
    }

    state.postShotOutcome = PostShotOutcome::None;
    state.postShotDelay = 0.0f;
}

void updateClouds(GameState& state, float deltaSeconds) {
    for (Cloud& cloud : state.clouds) {
        cloud.x += cloud.speed * deltaSeconds;
        if (cloud.x > kWindowWidth + 120.0f) {
            cloud.x = -140.0f;
            cloud.y = randomFloat(70.0f, 260.0f);
        }
    }
}

void simulateProjectileStep(Vec2& position, Vec2& velocity, float deltaSeconds, int wind) {
    velocity.x += wind * kWindAccelerationScale * deltaSeconds;
    velocity.y += kGravity * deltaSeconds;
    position.x += velocity.x * deltaSeconds;
    position.y += velocity.y * deltaSeconds;
}

void updateProjectile(GameState& state, float deltaSeconds) {
    if (!state.projectile.active) {
        return;
    }

    Vec2 nextPosition = state.projectile.position;
    Vec2 nextVelocity = state.projectile.velocity;
    simulateProjectileStep(nextPosition, nextVelocity, deltaSeconds, state.wind);

    state.projectile.position = nextPosition;
    state.projectile.velocity = nextVelocity;

    if (state.projectile.trail.empty()) {
        state.projectile.trail.push_back(nextPosition);
    } else {
        const Vec2& last = state.projectile.trail.back();
        const float dx = nextPosition.x - last.x;
        const float dy = nextPosition.y - last.y;
        if ((dx * dx + dy * dy) > 36.0f) {
            state.projectile.trail.push_back(nextPosition);
            if (state.projectile.trail.size() > 48) {
                state.projectile.trail.erase(state.projectile.trail.begin());
            }
        }
    }

    for (int index = 0; index < 2; ++index) {
        if (pointInRect(nextPosition, state.players[index].hitBox)) {
            const int winnerIndex = 1 - index;
            state.status = state.players[index].label + " recibio un impacto directo.";
            triggerExplosion(
                state,
                nextPosition,
                62.0f,
                winnerIndex == 0 ? PostShotOutcome::WinForPlayer1 : PostShotOutcome::WinForPlayer2);
            return;
        }
    }

    for (const Building& building : state.buildings) {
        if (pointInRect(nextPosition, building.bounds)) {
            state.status = "El disparo golpeo un edificio. Turno para el rival.";
            triggerExplosion(state, nextPosition, 46.0f, PostShotOutcome::NextTurn);
            return;
        }
    }

    if (nextPosition.x < -60.0f || nextPosition.x > kWindowWidth + 60.0f ||
        nextPosition.y < -80.0f || nextPosition.y > kWindowHeight + 80.0f) {
        state.projectile.active = false;
        state.status = "Fallo el disparo. Cambia el turno.";
        state.postShotOutcome = PostShotOutcome::NextTurn;
        state.postShotDelay = 0.55f;
    }
}

void updateExplosion(GameState& state, float deltaSeconds) {
    if (!state.explosion.active) {
        return;
    }

    state.explosion.radius += 220.0f * deltaSeconds;
    if (state.explosion.radius >= state.explosion.maxRadius) {
        state.explosion.active = false;
        applyPostShotOutcome(state);
    }
}

void updateGame(GameState& state, float deltaSeconds) {
    state.animationTime += deltaSeconds;
    updateClouds(state, deltaSeconds);

    if (state.scene != Scene::Playing) {
        return;
    }

    if (state.projectile.active) {
        updateProjectile(state, deltaSeconds);
    } else if (state.explosion.active) {
        updateExplosion(state, deltaSeconds);
    } else if (state.postShotDelay > 0.0f) {
        state.postShotDelay = std::max(0.0f, state.postShotDelay - deltaSeconds);
        if (state.postShotDelay == 0.0f) {
            applyPostShotOutcome(state);
        }
    }
}

void drawGradientSky(HDC hdc) {
    const COLORREF topColor = RGB(13, 24, 59);
    const COLORREF middleColor = RGB(95, 76, 145);
    const COLORREF bottomColor = RGB(252, 164, 104);

    for (int y = 0; y < kWindowHeight; ++y) {
        float t = static_cast<float>(y) / static_cast<float>(kWindowHeight);
        COLORREF color = t < 0.55f
            ? blendColor(topColor, middleColor, t / 0.55f)
            : blendColor(middleColor, bottomColor, (t - 0.55f) / 0.45f);
        drawLine(hdc, 0, y, kWindowWidth, y, color);
    }

    drawCircle(hdc, 1040, 185, 55, RGB(255, 230, 138), RGB(255, 245, 190), 2);
    drawCircle(hdc, 1040, 185, 78, blendColor(RGB(255, 230, 138), RGB(252, 164, 104), 0.55f),
               blendColor(RGB(255, 230, 138), RGB(252, 164, 104), 0.60f), 1);
}

void drawCloud(HDC hdc, const Cloud& cloud) {
    const COLORREF body = RGB(232, 226, 242);
    const COLORREF outline = RGB(211, 202, 229);
    const int baseX = static_cast<int>(cloud.x);
    const int baseY = static_cast<int>(cloud.y);
    const int size = static_cast<int>(cloud.size * 22.0f);

    drawCircle(hdc, baseX, baseY, size, body, outline);
    drawCircle(hdc, baseX + size, baseY + 4, size + 4, body, outline);
    drawCircle(hdc, baseX + size * 2, baseY, size, body, outline);
    drawCircle(hdc, baseX + size, baseY - 10, size - 4, body, outline);
}

void drawBackground(HDC hdc, const GameState& state) {
    drawGradientSky(hdc);

    for (const Star& star : state.stars) {
        drawCircle(hdc, star.x, star.y, star.radius, RGB(255, 250, 215), RGB(255, 250, 215));
    }

    for (const Cloud& cloud : state.clouds) {
        drawCloud(hdc, cloud);
    }

    const RECT farCity = makeRect(0, 360, kWindowWidth, kWindowHeight);
    fillSolidRect(hdc, farCity, RGB(27, 30, 56));

    for (int x = 0; x < kWindowWidth; x += 55) {
        const int height = 100 + ((x / 55) % 5) * 22;
        RECT silhouette = makeRect(x, 520 - height, x + 42, 520);
        fillSolidRect(hdc, silhouette, RGB(17, 18, 33));
    }
}

void drawBuildings(HDC hdc, const GameState& state) {
    for (const Building& building : state.buildings) {
        fillSolidRect(hdc, building.bounds, building.bodyColor);
        drawLine(hdc, building.bounds.left, building.bounds.top, building.bounds.right,
                 building.bounds.top, building.trimColor, 2);
        drawLine(hdc, building.bounds.right - 1, building.bounds.top,
                 building.bounds.right - 1, building.bounds.bottom, RGB(16, 18, 27), 1);

        const int windowWidth = 8;
        const int windowHeight = 12;
        int index = 0;
        for (int row = 0; row < building.windowRows; ++row) {
            for (int col = 0; col < building.windowCols; ++col) {
                const int x = building.bounds.left + 8 + col * 15;
                const int y = building.bounds.top + 10 + row * 19;
                const RECT windowRect = makeRect(x, y, x + windowWidth, y + windowHeight);
                const bool lit = index < static_cast<int>(building.litWindows.size()) && building.litWindows[index];
                fillSolidRect(hdc, windowRect, lit ? RGB(255, 214, 124) : RGB(47, 54, 74));
                ++index;
            }
        }
    }
}

void drawGorilla(HDC hdc, const Player& player, bool isCurrentTurn, float animationTime) {
    const int x = static_cast<int>(player.anchor.x);
    const int y = static_cast<int>(player.anchor.y);
    const COLORREF fur = RGB(49, 34, 27);
    const COLORREF outline = RGB(16, 12, 10);
    const COLORREF chest = RGB(112, 85, 61);

    if (isCurrentTurn) {
        const float bob = std::sin(animationTime * 4.0f) * 4.0f;
        drawCircle(hdc, x, y - 95 + static_cast<int>(bob), 12, player.accentColor, RGB(250, 250, 250), 2);
    }

    drawCircle(hdc, x, y - 30, 22, fur, outline, 2);
    drawCircle(hdc, x, y - 58, 15, fur, outline, 2);
    drawCircle(hdc, x, y - 27, 11, chest, fur, 1);

    drawLine(hdc, x - 16, y - 42, x - 28, y - 68 + (isCurrentTurn ? static_cast<int>(std::sin(animationTime * 4.0f) * 4.0f) : 0),
             outline, 5);
    drawLine(hdc, x + 16, y - 42, x + 28, y - 68 - (isCurrentTurn ? static_cast<int>(std::sin(animationTime * 4.0f) * 4.0f) : 0),
             outline, 5);
    drawLine(hdc, x - 10, y - 14, x - 15, y + 4, outline, 5);
    drawLine(hdc, x + 10, y - 14, x + 15, y + 4, outline, 5);

    drawCircle(hdc, x - 5, y - 61, 2, RGB(248, 248, 248), RGB(248, 248, 248));
    drawCircle(hdc, x + 5, y - 61, 2, RGB(248, 248, 248), RGB(248, 248, 248));
    drawCircle(hdc, x, y - 54, 3, player.accentColor, player.accentColor);
}

void drawPlayers(HDC hdc, const GameState& state) {
    for (int index = 0; index < 2; ++index) {
        const bool isCurrentTurn = state.scene == Scene::Playing &&
                                   state.currentPlayer == index &&
                                   !state.projectile.active &&
                                   !state.explosion.active;
        drawGorilla(hdc, state.players[index], isCurrentTurn, state.animationTime);
    }
}

void drawProjectileTrail(HDC hdc, const Projectile& projectile) {
    for (size_t index = 0; index < projectile.trail.size(); ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(std::max<size_t>(1, projectile.trail.size()));
        const int radius = 2 + static_cast<int>(t * 3.0f);
        const COLORREF color = blendColor(RGB(255, 170, 78), RGB(255, 245, 178), t);
        drawCircle(hdc,
                   static_cast<int>(projectile.trail[index].x),
                   static_cast<int>(projectile.trail[index].y),
                   radius, color, color);
    }
}

void drawProjectile(HDC hdc, const Projectile& projectile) {
    if (!projectile.active) {
        return;
    }

    drawProjectileTrail(hdc, projectile);
    const int x = static_cast<int>(projectile.position.x);
    const int y = static_cast<int>(projectile.position.y);
    drawCircle(hdc, x, y, 6, RGB(255, 236, 84), RGB(120, 95, 8), 2);
    drawLine(hdc, x - 5, y - 3, x + 4, y + 2, RGB(120, 95, 8), 1);
}

void drawExplosion(HDC hdc, const Explosion& explosion) {
    if (!explosion.active) {
        return;
    }

    const int x = static_cast<int>(explosion.position.x);
    const int y = static_cast<int>(explosion.position.y);
    const int outer = static_cast<int>(explosion.radius);
    drawCircle(hdc, x, y, outer, RGB(255, 122, 61), RGB(255, 245, 170), 2);
    drawCircle(hdc, x, y, std::max(10, outer - 12), RGB(255, 220, 88), RGB(255, 245, 170), 1);
    drawCircle(hdc, x, y, std::max(4, outer - 26), RGB(255, 245, 206), RGB(255, 245, 206), 1);
}

void drawWindBanner(HDC hdc, const GameState& state) {
    const RECT banner = makeRect(475, 18, 805, 72);
    drawRoundedPanel(hdc, banner, RGB(16, 22, 41), RGB(255, 198, 80));

    const std::string direction = state.wind < 0 ? "<<<<" : state.wind > 0 ? ">>>>" : "----";
    const std::string text = "Viento " + direction + "  " + formatNumber(static_cast<float>(std::abs(state.wind)));
    drawTextBlock(hdc, banner, text, g_uiFont, RGB(240, 242, 250),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// CAMBIO MANUAL 5:
// Mejora libre: se agrego una previsualizacion de la trayectoria para ayudar
// a entender visualmente el efecto del angulo, la velocidad y el viento.
void drawTrajectoryPreview(HDC hdc, const GameState& state) {
    if (!state.allowPreview || state.scene != Scene::Playing || state.projectile.active || state.explosion.active) {
        return;
    }

    float angle = state.input.phase == InputPhase::Angle
        ? (state.input.buffer.empty() ? state.lastAngle[state.currentPlayer]
                                      : static_cast<float>(std::stoi(state.input.buffer)))
        : state.input.angle;

    float speed = state.input.phase == InputPhase::Speed
        ? (state.input.buffer.empty() ? state.lastSpeed[state.currentPlayer]
                                      : static_cast<float>(std::stoi(state.input.buffer)))
        : state.lastSpeed[state.currentPlayer];

    angle = std::clamp(angle, 10.0f, 85.0f);
    speed = std::clamp(speed, 20.0f, 180.0f);

    Vec2 position = projectileSpawnPoint(state.players[state.currentPlayer]);
    Vec2 velocity{};
    const float direction = state.currentPlayer == 0 ? 1.0f : -1.0f;
    velocity.x = std::cos(degToRad(angle)) * speed * kVelocityScale * direction;
    velocity.y = -std::sin(degToRad(angle)) * speed * kVelocityScale;

    for (int step = 0; step < 70; ++step) {
        simulateProjectileStep(position, velocity, kPreviewStep, state.wind);

        bool collides = false;
        for (const Building& building : state.buildings) {
            if (pointInRect(position, building.bounds)) {
                collides = true;
                break;
            }
        }

        if (position.x < 0.0f || position.x > kWindowWidth ||
            position.y < 0.0f || position.y > kWindowHeight || collides) {
            break;
        }

        if (step % 3 == 0) {
            drawCircle(hdc,
                       static_cast<int>(position.x),
                       static_cast<int>(position.y),
                       3,
                       blendColor(RGB(255, 198, 80), RGB(112, 224, 255), state.currentPlayer == 1 ? 1.0f : 0.0f),
                       RGB(255, 246, 221),
                       1);
        }
    }
}

void drawScorePanel(HDC hdc, const GameState& state) {
    const RECT panel = makeRect(20, 18, 365, 140);
    drawRoundedPanel(hdc, panel, RGB(16, 22, 41), RGB(111, 224, 255));

    drawTextBlock(hdc, makeRect(38, 28, 320, 56), "Marcador", g_uiFont, RGB(250, 250, 250), DT_LEFT);
    drawTextBlock(hdc, makeRect(38, 60, 320, 90),
                  state.players[0].label + ": " + formatNumber(static_cast<float>(state.scores[0])),
                  g_smallFont, state.players[0].accentColor, DT_LEFT);
    drawTextBlock(hdc, makeRect(38, 88, 320, 118),
                  state.players[1].label + ": " + formatNumber(static_cast<float>(state.scores[1])),
                  g_smallFont, state.players[1].accentColor, DT_LEFT);
    drawTextBlock(hdc, makeRect(210, 60, 340, 118),
                  "Ronda " + formatNumber(static_cast<float>(state.roundNumber)),
                  g_smallFont, RGB(245, 245, 245), DT_LEFT);
}

void drawInfoPanel(HDC hdc, const GameState& state) {
    const RECT panel = makeRect(20, 560, 1260, 690);
    drawRoundedPanel(hdc, panel, RGB(16, 22, 41), RGB(255, 198, 80));

    // CAMBIO MANUAL 4:
    // La salida se mejoro con paneles que muestran turno, ultimo disparo,
    // estado actual y un bloque claro para escribir angulo y velocidad.
    drawTextBlock(hdc, makeRect(40, 578, 570, 610),
                  "Turno actual: " + currentPlayerName(state),
                  g_uiFont, RGB(250, 250, 250), DT_LEFT);
    drawTextBlock(hdc, makeRect(40, 615, 900, 650), state.status,
                  g_smallFont, RGB(232, 236, 248), DT_LEFT | DT_WORDBREAK);

    const std::string lastShot1 = state.hasLastShot[0]
        ? "J1 ultimo tiro: angulo " + formatNumber(state.lastAngle[0]) +
          " | velocidad " + formatNumber(state.lastSpeed[0])
        : "J1 ultimo tiro: aun no disparo";
    const std::string lastShot2 = state.hasLastShot[1]
        ? "J2 ultimo tiro: angulo " + formatNumber(state.lastAngle[1]) +
          " | velocidad " + formatNumber(state.lastSpeed[1])
        : "J2 ultimo tiro: aun no disparo";

    drawTextBlock(hdc, makeRect(600, 580, 930, 610), lastShot1,
                  g_smallFont, state.players[0].accentColor, DT_LEFT | DT_WORDBREAK);
    drawTextBlock(hdc, makeRect(600, 615, 930, 645), lastShot2,
                  g_smallFont, state.players[1].accentColor, DT_LEFT | DT_WORDBREAK);

    const RECT inputPanel = makeRect(945, 578, 1235, 670);
    drawRoundedPanel(hdc, inputPanel, RGB(27, 35, 62), RGB(111, 224, 255));

    const std::string phaseText = state.input.phase == InputPhase::Angle
        ? "Entrada: angulo"
        : "Entrada: velocidad";
    drawTextBlock(hdc, makeRect(964, 590, 1210, 620), phaseText,
                  g_smallFont, RGB(255, 245, 215), DT_LEFT);
    drawTextBlock(hdc, makeRect(964, 622, 1210, 664),
                  state.input.buffer.empty() ? "_" : state.input.buffer,
                  g_inputFont, RGB(250, 250, 250), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void drawHelpOverlay(HDC hdc) {
    const RECT help = makeRect(915, 90, 1235, 250);
    drawRoundedPanel(hdc, help, RGB(16, 22, 41), RGB(233, 230, 165));
    drawTextBlock(hdc, makeRect(936, 104, 1210, 130), "Controles",
                  g_uiFont, RGB(250, 250, 250), DT_LEFT);
    drawTextBlock(hdc, makeRect(936, 138, 1210, 232),
                  "Escribe numeros y Enter.\nBackspace borra.\nH muestra u oculta ayuda.\nR reinicia la partida.\nM vuelve al menu.",
                  g_smallFont, RGB(232, 236, 248), DT_LEFT | DT_WORDBREAK);
}

void drawMenuOverlay(HDC hdc, const GameState& state) {
    const RECT card = makeRect(190, 110, 1090, 510);
    drawRoundedPanel(hdc, card, RGB(16, 22, 41), RGB(255, 198, 80));

    drawTextBlock(hdc, makeRect(220, 145, 1060, 220), "GORILLAS RELOADED",
                  g_titleFont, RGB(250, 250, 250), DT_CENTER | DT_SINGLELINE);
    drawTextBlock(hdc, makeRect(230, 225, 1050, 290),
                  "Version en C++ inspirada en Gorilla.bas",
                  g_subtitleFont, RGB(255, 214, 124), DT_CENTER | DT_SINGLELINE);
    drawTextBlock(hdc, makeRect(260, 300, 1010, 390),
                  "Dos jugadores se turnan para lanzar bananas explosivas.\n"
                  "La trayectoria depende del angulo, la velocidad, la gravedad y el viento.\n"
                  "Gana quien obtiene 2 rondas primero.",
                  g_uiFont, RGB(234, 237, 248), DT_CENTER | DT_WORDBREAK);
    drawTextBlock(hdc, makeRect(260, 418, 1010, 482),
                  "Enter: jugar  |  H: ayuda  |  M: refrescar fondo",
                  g_smallFont, RGB(112, 224, 255), DT_CENTER | DT_SINGLELINE);

    if (!state.roundBanner.empty()) {
        drawTextBlock(hdc, makeRect(240, 470, 1040, 500), state.status,
                      g_smallFont, RGB(255, 245, 215), DT_CENTER | DT_SINGLELINE);
    }
}

void drawRoundOverlay(HDC hdc, const GameState& state, bool matchOver) {
    const RECT card = makeRect(290, 190, 990, 470);
    drawRoundedPanel(hdc, card, RGB(16, 22, 41), matchOver ? RGB(255, 198, 80) : RGB(111, 224, 255));
    drawTextBlock(hdc, makeRect(330, 230, 950, 300), state.roundBanner,
                  g_subtitleFont, RGB(250, 250, 250), DT_CENTER | DT_WORDBREAK);
    drawTextBlock(hdc, makeRect(340, 314, 940, 372),
                  "Marcador: " + state.players[0].label + " " + formatNumber(static_cast<float>(state.scores[0])) +
                  " - " + formatNumber(static_cast<float>(state.scores[1])) + " " + state.players[1].label,
                  g_uiFont, RGB(232, 236, 248), DT_CENTER | DT_WORDBREAK);
    drawTextBlock(hdc, makeRect(340, 390, 940, 436), state.status,
                  g_smallFont, RGB(255, 245, 215), DT_CENTER | DT_WORDBREAK);
}

void drawScene(HDC hdc, const GameState& state) {
    drawBackground(hdc, state);
    drawBuildings(hdc, state);
    drawTrajectoryPreview(hdc, state);
    drawPlayers(hdc, state);
    drawProjectile(hdc, state.projectile);
    drawExplosion(hdc, state.explosion);
    drawWindBanner(hdc, state);
    drawScorePanel(hdc, state);
    drawInfoPanel(hdc, state);

    if (state.showHelp) {
        drawHelpOverlay(hdc);
    }

    if (state.scene == Scene::Menu) {
        drawMenuOverlay(hdc, state);
    } else if (state.scene == Scene::RoundOver) {
        drawRoundOverlay(hdc, state, false);
    } else if (state.scene == Scene::MatchOver) {
        drawRoundOverlay(hdc, state, true);
    }
}

void handleDigitInput(GameState& state, char character) {
    if (state.scene != Scene::Playing || state.projectile.active || state.explosion.active) {
        return;
    }

    if (std::isdigit(static_cast<unsigned char>(character))) {
        if (state.input.buffer.size() < 3) {
            state.input.buffer.push_back(character);
        }
    } else if (character == '\b') {
        if (!state.input.buffer.empty()) {
            state.input.buffer.pop_back();
        }
    } else if (character == '\r') {
        if (state.input.buffer.empty()) {
            return;
        }

        const int value = std::stoi(state.input.buffer);
        if (state.input.phase == InputPhase::Angle) {
            state.input.angle = static_cast<float>(std::clamp(value, 10, 85));
            state.input.phase = InputPhase::Speed;
            state.input.buffer.clear();
            state.status = currentPlayerName(state) + ": ingresa la velocidad (20 a 180) y presiona Enter.";
        } else {
            state.input.speed = static_cast<float>(std::clamp(value, 20, 180));
            state.lastAngle[state.currentPlayer] = state.input.angle;
            state.lastSpeed[state.currentPlayer] = state.input.speed;
            state.hasLastShot[state.currentPlayer] = true;
            launchProjectile(state, state.input.angle, state.input.speed);
        }
    }
}

int runSelfTest() {
    GameState state;
    seedSkyDecor(state);
    backToMenu(state);
    startMatch(state);

    std::ostringstream report;
    report << "Self-test OK\n";
    report << "Buildings: " << state.buildings.size() << "\n";
    report << "Wind: " << state.wind << "\n";
    report << "Player 1 x: " << state.players[0].anchor.x << "\n";
    report << "Player 2 x: " << state.players[1].anchor.x << "\n";

    const float sampleAngle = 48.0f;
    const float sampleSpeed = 74.0f;
    launchProjectile(state, sampleAngle, sampleSpeed);
    for (int i = 0; i < 20; ++i) {
        updateGame(state, 0.016f);
    }
    report << "Projectile active after 20 frames: " << (state.projectile.active ? "yes" : "no") << "\n";
    report << "Trail points: " << state.projectile.trail.size() << "\n";

    std::printf("%s", report.str().c_str());
    return 0;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createFonts();
            seedSkyDecor(g_state);
            backToMenu(g_state);
            SetTimer(window, kFrameTimerId, kFrameTimerMs, nullptr);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_TIMER: {
            const auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> delta = now - g_state.lastUpdate;
            g_state.lastUpdate = now;
            updateGame(g_state, std::min(delta.count(), 0.05f));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }

        case WM_CHAR:
            handleDigitInput(g_state, static_cast<char>(wParam));
            InvalidateRect(window, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_RETURN:
                    if (g_state.scene == Scene::Menu) {
                        startMatch(g_state);
                    } else if (g_state.scene == Scene::RoundOver) {
                        startRound(g_state);
                    } else if (g_state.scene == Scene::MatchOver) {
                        startMatch(g_state);
                    }
                    break;

                case 'H':
                    g_state.showHelp = !g_state.showHelp;
                    break;

                case 'R':
                    startMatch(g_state);
                    break;

                case 'M':
                    backToMenu(g_state);
                    break;

                case VK_ESCAPE:
                    DestroyWindow(window);
                    break;
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC screen = BeginPaint(window, &paint);
            HDC memory = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, kWindowWidth, kWindowHeight);
            HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

            drawScene(memory, g_state);

            BitBlt(screen, 0, 0, kWindowWidth, kWindowHeight, memory, 0, 0, SRCCOPY);
            SelectObject(memory, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(window, kFrameTimerId);
            destroyFonts();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(window, message, wParam, lParam);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--self-test") {
        return runSelfTest();
    }

    const HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != nullptr) {
        ShowWindow(consoleWindow, SW_HIDE);
    }

    HINSTANCE instance = GetModuleHandle(nullptr);
    const char* className = "GorillasReloadedWindow";

    WNDCLASSA windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassA(&windowClass);

    RECT client = makeRect(0, 0, kWindowWidth, kWindowHeight);
    AdjustWindowRect(&client, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    const int windowWidth = client.right - client.left;
    const int windowHeight = client.bottom - client.top;

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenWidth - windowWidth) / 2;
    const int y = (screenHeight - windowHeight) / 2;

    g_window = CreateWindowExA(
        0,
        className,
        "Gorillas Reloaded - C++",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (g_window == nullptr) {
        return 1;
    }

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);

    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return static_cast<int>(message.wParam);
}