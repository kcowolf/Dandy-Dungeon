#include <algorithm>
#include <map>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL.h>
#include <SDL_image.h>

#ifndef MAX_PATH
	#define MAX_PATH 255
#endif

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
static bool gQuit = false;
static SDL_Window* gWindow = nullptr;
static SDL_Renderer* gRenderer = nullptr;
static SDL_Texture* gTexture = nullptr;

static bool init();
static bool loadMedia();
static void close();

// The game goes here

void MyDebugBreak()
{
	printf("MyDebugBreak hit!\n");
}

void MyAssert(bool test)
{
	if(!test)
	{
		printf("Failed assert!\n");
	}
}

enum Direction
{
	kDirUp,
	kDirUpRight,
	kDirRight,
	kDirDownRight,
	kDirDown,
	kDirDownLeft,
	kDirLeft,
	kDirUpLeft,
	kDirNone = 0xff
};
enum MapData
{
	kSpace,
	kWall,
	kLock,
	kUp,
	kDown,
	kKey,
	kFood,
	kMoney,
	kBomb,
	kGhost,
	kSmiley,
	kBig,
	kHeart,
	kGen1,
	kGen2,
	kGen3,
	kArrow0, // Down-left arrow
	kArrow1,
	kArrow2,
	kArrow3,
	kArrow4,
	kArrow5,
	kArrow6,
	kArrow7,
	kPlayer0, // Actually has a "1" on his cheast
	kPlayer1,
	kPlayer2,
	kPlayer3,

	MAP_DATA_COUNT
};

static SDL_Rect gTileClips[MapData::MAP_DATA_COUNT];

class Map
{
public:
	Map()
	{
		Init();
	}

	MapData Get(uint32_t x, uint32_t y)
	{
		MapData b = kSpace;
		if(x >= 0 && x < Width && y >= 0 && y < Height)
		{
			b = (MapData) Cell[x + y*Width];
		}
		else
		{
			MyDebugBreak();
		}
		return b;
	}

	MapData Get(uint32_t x, uint32_t y, Direction dir)
	{
		MapData b = kSpace;
		if(x >= 0 && x < Width && y >= 0 && y < Height)
		{
			b = (MapData) Cell[x + y*Width];
		}
		else
		{
			MyDebugBreak();
		}
		return b;
	}

	void Set(uint32_t x, uint32_t y, int v)
	{
		if(x >= 0 && x < Width && y >= 0 && y < Height && v <= kPlayer3)
		{
			Cell[x + y*Width] = v;
		}
		else
		{
			MyDebugBreak();
		}
	}

	bool Find(uint8_t& rx, uint8_t& ry, MapData v)
	{
		for(int y = 0; y < Height; y++)
		{
			for(int x = 0; x < Width; x++)
			{
				if(Cell[x + y * Width] == v)
				{
					rx = x;
					ry = y;
					return true;
				}
			}
		}
		return false;
	}

	void OpenLock(uint32_t x, uint32_t y)
	{
		// Flood fill from this coord
		if(Cell[x + y * Width] == kLock)
		{
			Cell[x + y * Width] = kSpace;
			for(int dy = -1;dy <= 1; dy++)
				for(int dx = -1;dx <= 1; dx++)
					if(dx != 0 || dy != 0)
						OpenLock(x + dx, y + dy);
		}
	}

	void Init()
	{
		for(uint32_t y = 0; y < Height; y++)
		{
			for(uint32_t x = 0; x < Width; x++)
			{
				uint8_t b = kSpace;
				if(y == 0 || y == Height-1 || x == 0 || x == Width - 1)
				{
					b = kWall;
				}
				else if ( x == 2 && y == 2)
				{
					b = kUp;
				}
				else if ( x == 10 && y == 10 )
				{
					b = kDown;
				}
				Cell[y*Width+x] = b;
			}
		}
	}

	bool LoadLevel(uint32_t index)
	{
		char fileName[MAX_PATH];
		FILE* in;
		sprintf(fileName, "levels\\level.%c", index + 'a');
		if((in = fopen(fileName, "rb")) == NULL)
		{
			sprintf(fileName, "..\\levels\\level.%c", index + 'a');
			in = fopen(fileName, "rb");
		}
		bool failed = true;
		if(in)
		{
			failed = false;
			for(int y = 0; y < Height; y++)
			{
				for(int x = 0; x < Width; x += 2)
				{
					int inb = fgetc(in);
					if(inb < 0)
					{
						failed = true;
						break;
					}
					Cell[y*Width+x] = (uint8_t) (inb & 0xf);
					Cell[y*Width+x+1] = (uint8_t) ((inb >> 4) & 0xf);
				}
			}
			fclose(in);
		}
		if(failed)
		{
			Init();
		}
		return !failed;
	}

	void GetActive(float& x, float& y, uint32_t& left, uint32_t& top, uint32_t& right, uint32_t& bottom)
	{
		GetActive1(x, left, right, Map::Width, Map::ViewWidth);
		GetActive1(y, top, bottom, Map::Height, Map::ViewHeight);
	}

	void GetActive1(float& x, uint32_t& left, uint32_t& right, uint32_t width, uint32_t viewWidth)
	{
		x -= (viewWidth / 2.0f);
		x = std::max(x, 0.f);
		x = std::min(x, (float)(width - viewWidth));
		left = (uint32_t) x;
		right = std::min(left + viewWidth + 1, width);
	}

	const static uint32_t Width = 60;
	const static uint32_t Height = 30;
	const static uint32_t NumCells = Width * Height;
	uint8_t Cell[NumCells];

	const static uint32_t ViewWidth = 20;
	const static uint32_t ViewHeight = 10;
};

class Arrow
{
public:
	Arrow()
	{
		alive = false;
		x = 0;
		y = 0;
		dir = kDirNone;
	}

	static bool CanGo(MapData d)
	{
		return d == kSpace;
	}

	static bool CanHit(MapData d)
	{
		return d >= kBomb && d <= kGen3;
	}

	bool alive;
	uint8_t x;
	uint8_t y;
	Direction dir;
};

enum PlayerState
{
	kNormal,
	kInWarp
};

class Player
{
public:
	Player()
	{
		Init();
	}

	void Init()
	{
		x = 0;
		y = 0;
		state = kNormal;
		health = kHealthMax;
		food = 0;
		bombs = 0;
		keys = 0;
		score = 0;
		dir = kDirNone;
		lastMoveTime = 0;
	}

	bool IsAlive()
	{
		return health > 0;
	}

	bool IsVisible()
	{
		return health > 0 && state == kNormal;
	}

	void EatFood()
	{
		if(food > 0 && health < kHealthMax)
		{
			--food;
			health = kHealthMax;
		}
	}

	static const int kHealthMax = 9;
	uint8_t x;
	uint8_t y;
	uint8_t health;
	uint8_t food;
	uint8_t keys;
	uint8_t bombs;
	uint32_t score;
	PlayerState state;
	uint32_t lastMoveTime;
	Direction dir;
	Arrow arrow;
};

class World
{
public:
	World()
	{
	}

	void Init()
	{
		map.Init();
		numPlayers = 1;
		for(uint32_t i = 0; i < numPlayers; i++)
		{
			player[i].Init();
		}
	}

	void Update()
	{
		time++;

		for(uint32_t i = 0; i < numPlayers; i++)
		{
			DoArrowMove(&player[i], false);
		}

		DoMonsters();
	}

	bool IsGameOver()
	{
		for(uint32_t i = 0; i < numPlayers; i++)
		{
			if(player[i].IsAlive())
			{
				return false;
			}
		}
		return true;
	}

	void DoMonsters()
	{
		float cogX;
		float cogY;
		uint32_t startX;
		uint32_t endX;
		uint32_t startY;
		uint32_t endY;
		GetCOG(cogX, cogY);
		map.GetActive(cogX, cogY, startX, startY, endX, endY);

		// update in a grid pattern
		int gridStep = time % 9;
		int gridXOffset = gridStep % 3;
		int gridYOffset = gridStep / 3;
		for(uint32_t y = startY + gridYOffset; y < endY; y += 3)
		{
			for(uint32_t x = startX + gridXOffset; x < endX; x += 3)
			{
				MapData d = map.Get(x, y);
				if(d >= kGhost && d <= kBig)
				{
					// Move towards nearest player
					Direction dir = GetDirectionOfNearestPlayer(x, y);
					if(dir != kDirNone)
					{
						uint8_t mx;
						uint8_t my;
						bool canMove = false;
						MapData d2;
						for(int test = 0; test < 3; test++)
						{
							const static int kTestDelta[3] = {0,-1,1};
							mx = (uint8_t) x;
							my = (uint8_t) y;
							MoveCoords(mx, my, (dir + kTestDelta[test]) & 7);
							d2 = map.Get(mx, my);
							if(d2 == kSpace || d2 >= kPlayer0 && d2 <= kPlayer3)
							{
								canMove = true;
								break;
							}
						}
						if(canMove)
						{
							map.Set(x, y, kSpace);
							if(d2 >= kPlayer0 && d2 <= kPlayer3)
							{
								Player* p = &player[d2 - kPlayer0];
								int monsterHit = d - kGhost + 1;
								if(p->health > monsterHit)
								{
									p->health -= monsterHit;
								}
								else
								{
									p->health = 0;
									MapData remains = kSpace;
									if(p->keys)
									{
										--p->keys;
										remains = kKey;
									}
									map.Set(p->x, p->y, remains);
								}
							}
							else
							{
								map.Set(mx, my, d);
							}
						}
					}
				}
				else if(d >= kGen1 && d <= kGen3)
				{
					// Random generator
					if(getRandom(10) < 3)
					{
						uint8_t gx = (uint8_t) x;
						uint8_t gy = (uint8_t) y;
						MoveCoords(gx, gy, getRandom(4) * 2);
						if(map.Get(gx,gy) == kSpace)
						{
							map.Set(gx, gy, (MapData) kGhost + (d - kGen1));
						}
					}
				}
			}
		}
	}

	static uint32_t getRandom(uint32_t range)
	{
		return rand() % range;
	}

	Direction GetDirectionOfNearestPlayer(uint32_t x, uint32_t y)
	{
		uint32_t bestX = 0;
		uint32_t bestY = 0;
		uint32_t bestDistance = 10000;
		for(uint32_t i = 0; i < numPlayers; i++)
		{
			Player *pP = &player[i];
			if(pP->IsVisible())
			{
				uint32_t distance = abs((int) (pP->x - x)) + abs((int) (pP->y - y));
				if(distance < bestDistance)
				{
					bestDistance = distance;
					bestX = pP->x;
					bestY = pP->y;
				}
			}
		}
		if(bestDistance == 10000)
		{
			return kDirNone;
		}
		int dx = bestX - x;
		int dy = bestY - y;
		uint8_t bitField = 0;
		if(dy > 0) bitField |= 8;
		else if(dy < 0) bitField |= 4;
		if(dx > 0) bitField |= 2;
		else if(dx < 0) bitField |= 1;

		//     7 0 1
		//     6 + 2 
		//     5 4 3 

		const static uint8_t kDirTable[16] =
		{
			   // YyXx
			255, // 0000
			6, // 0001
			2, // 0010
			255, // 0011
			0, // 0100
			7, // 0101
			1, // 0110
			255, // 0111
			4, // 1000
			5, // 1001
			3, // 1010
			255, // 1011
			255, // 1100
			255, // 1101
			255, // 1110
			255, // 1111
		};

		return (Direction) kDirTable[bitField];
	}

	void GetCOG(float& x, float& y)
	{
		x = 0.f;
		y = 0.f;
		int liveCount = 0;
		for(uint32_t i = 0; i < numPlayers; i++)
		{
			Player *pP = &player[i];
			if(pP->IsVisible())
			{
				x += pP->x;
				y += pP->y;
				++liveCount;
			}
		}
		if(liveCount)
		{
			x /= liveCount;
			y /= liveCount;
		}
	}

	void LoadLevel(uint32_t index)
	{
		if(map.LoadLevel(index))
		{
			level = (uint8_t) index;
		}
		else
		{
			level = 0;
			map.LoadLevel(0);
		}
		SetPlayerPositions();
	}

	void ChangeLevel(int delta)
	{
		uint32_t newLevel = std::min(26, level + delta);
		LoadLevel(newLevel);
	}

	void SetPlayerPositions()
	{
		uint8_t x;
		uint8_t y;
		if(!map.Find(x, y, kUp))
		{
			MyDebugBreak();
			x = 4;
			y = 4;
		}
		for(uint32_t i = 0; i < numPlayers; i++)
		{
			Player* p = &player[i];
			if(p->IsAlive())
			{
				uint8_t px = x;
				uint8_t py = y;
				MoveCoords(px, py, i * 2);
				PlaceInWorld(i, px, py);
			}
		}
	}

	void PlaceInWorld(uint32_t index, uint32_t x, uint32_t y)
	{
		Player* p = &player[index];
		MyAssert(p->IsAlive());
		p->x = (uint8_t) x;
		p->y = (uint8_t) y;
		p->dir = (Direction) (index * 2);
		map.Set(p->x, p->y, (MapData) (kPlayer0 + index));
		p->state = kNormal;
		p->arrow.alive = false;
	}

	void Move(uint32_t stick, Direction dir)
	{
		if(stick < 4 && dir < 8)
		{
			if(stick < numPlayers)
			{
				Player* p = &player[stick];
				p->dir = dir;
				if(p->IsVisible() && time - p->lastMoveTime >= FRAMES_PER_MOVE)
				{
					p->lastMoveTime = time;
					uint8_t x = p->x;
					uint8_t y = p->y;
					MoveCoords(x, y, dir);
					MapData d = map.Get(x,y);
					bool bMove = false;
					switch(d)
					{
					case kSpace:
						bMove = true;
						break;
					case kLock:
						if(p->keys)
						{
							--p->keys;
							map.OpenLock(x, y);
							bMove = true;
						}
						break;
					case kKey:
						++p->keys;
						bMove = true;
						break;
					case kFood:
						++p->food;
						bMove = true;
						break;
					case kMoney:
						p->score += 10;
						bMove = true;
						break;
					case kBomb:
						++p->bombs;
						bMove = true;
						break;
					case kDown:
						{
							p->state = kInWarp;
							map.Set(p->x, p->y, kSpace);
							if(IsPartyInWarp())
							{
								ChangeLevel(1);
							}
						}
						break;
					default:
						break;
					}
					if(bMove)
					{
						map.Set(p->x, p->y, kSpace);
						map.Set(x, y, kPlayer0 + stick);
						p->x = x;
						p->y = y;
					}
				}

			}
		}
		else
		{
			MyDebugBreak();
		}
	}

	bool IsPartyInWarp()
	{
		// At least one player in warp, and no players visible
		bool atLeastOneWarp = false;
		bool atLeastOneVisible = false;
		for(uint32_t i = 0; i < numPlayers;i++)
		{
			if(player[i].IsVisible())
			{
				atLeastOneVisible = true;
				break;
			}
			if(player[i].IsAlive() && player[i].state == kInWarp)
			{
				atLeastOneWarp = true;
			}
		}
		if(atLeastOneWarp && ! atLeastOneVisible)
		{
			return true;
		}
		return false;
	}

	void EatFood(uint32_t index)
	{
		if(index < numPlayers)
		{
			Player* p = &player[index];
			if(p->IsVisible())
			{
				p->EatFood();
			}
		}
	}

	void Fire(uint32_t index)
	{
		if(index < numPlayers)
		{
			Player* p = &player[index];
			if(!p->arrow.alive)
			{
				p->arrow.alive = true;
				p->arrow.x = p->x;
				p->arrow.y = p->y;
				p->arrow.dir = p->dir;
				DoArrowMove(p, true);
			}
		}
		else
		{
			MyDebugBreak();
		}
	}

	void DoArrowMove(Player* p, bool isFirstMove)
	{
		if(!p->arrow.alive)
		{
			return;
		}
		uint8_t x = p->arrow.x;
		uint8_t y = p->arrow.y;
		if(!isFirstMove)
		{
			map.Set(x, y, kSpace);
		}
		MoveCoords(x, y, p->arrow.dir);
		MapData d = map.Get(x,y);
		if(Arrow::CanHit(d))
		{
			switch(d)
			{
			case kBomb:
				DoSmartBomb();
				map.Set(x, y, kSpace);
				break;
			case kGhost:
			case kSmiley:
			case kBig:
			case kGen1:
			case kGen2:
			case kGen3:
				map.Set(x, y, kSpace);
				break;
			case kHeart:
				{
					bool foundPlayer = false;
					for(uint32_t i = 0; i < numPlayers; i++)
					{
						Player* p = &player[i];
						if(!p->IsAlive())
						{
							p->health = 9;
							p->state = kNormal;
							PlaceInWorld(i, x, y);
							foundPlayer = true;
							break;
						}
					}
					if(!foundPlayer)
					{
						map.Set(x, y, kBig);
					}
				}
				break;
			default:
				MyDebugBreak();
			}
			p->arrow.alive = false;
		}
		else if(Arrow::CanGo(d))
		{
			p->arrow.x = x;
			p->arrow.y = y;
			int rotatedDir = ((p->arrow.dir + 3) & 7); // Because font is screwed up
			map.Set(x, y, kArrow0 + rotatedDir);
		}
		else
		{
			p->arrow.alive = false;
		}
	}

	void UseSmartBomb(uint32_t index)
	{
		if(index < numPlayers)
		{
			Player* p = &player[index];
			if(p->bombs)
			{
				--p->bombs;
				DoSmartBomb();
			}
		}
		else
		{
			MyDebugBreak();
		}
	}

	void DoSmartBomb()
	{
		float cogX;
		float cogY;
		uint32_t startX;
		uint32_t endX;
		uint32_t startY;
		uint32_t endY;
		GetCOG(cogX, cogY);
		map.GetActive(cogX, cogY, startX, startY, endX, endY);
		for(uint32_t y = startX; y < endY; y++)
		{
			for(uint32_t x = startX; x < endX; x++)
			{
				MapData d = map.Get(x, y);
				if(d >= kGhost && d <= kBig || d >= kGen1 && d <= kGen3)
				{
					map.Set(x, y, kSpace);
				}
			}
		}
	}

	static void MoveCoords(uint8_t& x, uint8_t& y, uint32_t direction)
	{
		if(direction < 8)
		{
			// Up is zero, clockwise
			static signed char kOffsets[8][2] =
				{
					{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}
				};
			x += kOffsets[direction][0];
			y += kOffsets[direction][1];
		}
		else
		{
			MyDebugBreak();
		}
	}
	Map map;
	uint8_t level;
	const static int PlayerCount = 4;
	Player player[PlayerCount];
	uint32_t numPlayers;
	uint32_t time = 0;

	static const uint32_t FRAMES_PER_MOVE = 3;
};

class GamePad
{
public:
	GamePad()
	{
		buttons = 0;
		strobe = 0;
	}

	static const int kUp = 1; // Mask for up button
	static const int kDown = 2;
	static const int kLeft = 4;
	static const int kRight = 8;
	static const int kA = 16;
	static const int kB = 32;
	static const int kC = 64;
	static const int kD = 128;
	uint8_t buttons; // bit set if button is currently pressed down
	uint8_t strobe; // bit set if button is newly pressed down
};

class Keyboard
{
public:
	Keyboard()
	{
	}
	void HandleEvent(bool down, uint8_t key)
	{
		data[key] = down;
	}
	static const int KeySize = 256;
	std::map<SDL_Keycode, bool> data;
};

const uint32_t kNumVerts = Map::NumCells * 6;

class View
{
public:
	View()
	{
	}

	void Render(World& world)
	{
		float x;
		float y;
		world.GetCOG(x, y);
		DrawToTexture(world.map, x, y);
	}

	void DrawToTexture(Map& map, float cogX, float cogY)
	{
		uint32_t startX;
		uint32_t endX;
		uint32_t startY;
		uint32_t endY;

		const float xBase = -cogX * 16.f - 0.5f;
		const float yBase = -cogY * 16.f - 0.5f;
		const float CellSize = 16.0f;

		map.GetActive(cogX, cogY, startX, startY, endX, endY);

		for (uint32_t x = startX; x < endX; x++)
		{
			for (uint32_t y = startY; y < endY; y++)
			{
				uint8_t b = map.Get(x, y);
				
				SDL_Rect destRect = {
					(xBase + x * CellSize) + (16 * 10),
					(yBase + y * CellSize) + (16 * 6),
					16,
					16
				};

				SDL_RenderCopy(gRenderer, gTexture, &gTileClips[b], &destRect);
			}
		}
	}
};

class Game
{
public:
	Game()
	{
		Init();
	}

	void Init()
	{
		world.Init();
	}

	void Start()
	{
		Init();
		world.LoadLevel(0);
	}
	void Render()
	{
		view.Render(world);
	}

	void HandleEvent(bool down, SDL_Keycode key)
	{
		keyboard.HandleEvent(down, key);
	}

	void TranslateKeysToPads()
	{
		struct PadMapEntry {
			SDL_Keycode vkcode;
			uint8_t pad;
			uint8_t mask;
		};
		PadMapEntry map[] = {
			// ASDW
			{SDLK_a, 0, GamePad::kLeft},
			{SDLK_s, 0, GamePad::kDown},
			{SDLK_d, 0, GamePad::kRight},
			{SDLK_w, 0, GamePad::kUp},
			{SDLK_SPACE, 0, GamePad::kA},
			{SDLK_1, 0, GamePad::kB},
			{SDLK_2, 0, GamePad::kC},

			{SDLK_9, 0, GamePad::kD}, // For development go down to next level

			// Number pad
			{SDLK_KP_4, 1, GamePad::kLeft},
			{SDLK_KP_5, 1, GamePad::kDown},
			{SDLK_KP_6, 1, GamePad::kRight},
			{SDLK_KP_8, 1, GamePad::kUp},
			{SDLK_KP_0, 1, GamePad::kA},
			{SDLK_KP_7, 1, GamePad::kB},
			{SDLK_KP_9, 1, GamePad::kC},
			{SDLK_UNKNOWN, 0, 0}
		};

		// Reset all pads
		for(int i = 0; i < World::PlayerCount; i++)
		{
			gamepad[i].strobe = gamepad[i].buttons; // Remember old state
			gamepad[i].buttons = 0;
		}
		for(PadMapEntry* pE = map; pE->vkcode != 0; pE++)
		{
			if(keyboard.data[pE->vkcode])
			{
				gamepad[pE->pad].buttons |= pE->mask;
			}
		}
		// calculate strobe
		for(int i = 0; i < World::PlayerCount; i++)
		{
			gamepad[i].strobe = gamepad[i].buttons & ~ gamepad[i].strobe;
		}
	}

	void Step()
	{
		world.Update();
		TranslateKeysToPads();
		MovePlayers();
		if(world.IsGameOver())
		{
			Start();
		}
	}

	void MovePlayers()
	{
		for(uint32_t i = 0; i < world.numPlayers; i++)
		{
			GamePad* pPad = & gamepad[i];
			static Direction kPadToDir[] =
			{
				// Bitfield is Right Left Down Up
				// Directions are clockwise from up == 0
				kDirNone, // 0000
				kDirUp, // 0001 
				kDirDown, // 0010
				kDirNone, // 0011 
				kDirLeft, // 0100
				kDirUpLeft, // 0101
				kDirDownLeft, // 0110
				kDirLeft, // 0111
				kDirRight, // 1000
				kDirUpRight, // 1001
				kDirDownRight, // 1010
				kDirRight, // 1011
				kDirNone, // 1100
				kDirUp, // 1101 
				kDirDown, // 1110 
				kDirNone, // 1111 
			};
			Direction dir = kPadToDir[0xf & pPad->buttons];
			if(dir != kDirNone)
			{
				world.Move(i, dir);
			}

			// Handle strobes
			if(pPad->buttons & GamePad::kA)
			{
				world.Fire(i);
			}
			if(pPad->strobe & GamePad::kB)
			{
				world.EatFood(i);
			}
			if(pPad->strobe & GamePad::kC)
			{
				world.UseSmartBomb(i);
			}

			if(pPad->strobe & GamePad::kD)
			{
				if(i == 0)
				{
					world.ChangeLevel(1); // For debugging
				}
			}
		}
	}

	World world;
	GamePad gamepad[World::PlayerCount];
	Keyboard keyboard;
	View view;
};

Game gGame;

static bool init()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("Failed to initialize SDL.  SDL error: %s\n", SDL_GetError());
		return false;
	}

	gWindow = SDL_CreateWindow("Dandy Dungeon", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
	if (gWindow == nullptr)
	{
		printf("Window could not be created.  SDL error: %s\n", SDL_GetError());
		return false;
	}

	gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (gRenderer == nullptr)
	{
		printf("Renderer could not be created.  SDL error: %s\n", SDL_GetError());
		return false;
	}

	// 16x16 = size of tiles in dandy.bmp
	if (SDL_RenderSetLogicalSize(gRenderer, 16 * 20 + 6, 16 * 10 + 34) < 0)
	{
		printf("Failed to set logical size.  SDL error: %s\n", SDL_GetError());
		return false;
	}

	SDL_SetRenderDrawColor(gRenderer, 0xFF, 0xFF, 0xFF, 0xFF);

	int imgFlags = IMG_INIT_PNG;
	if (!(IMG_Init(imgFlags) & imgFlags))
	{
		printf("Failed to initialize SDL_image.  SDL_image error: %s\n", IMG_GetError());
		return false;
	}

	gGame.Start();

	return true;
}

static bool loadMedia()
{
	SDL_Surface* loadedSurface = IMG_Load("dandy.bmp");
	SDL_Texture* texture = nullptr;
	if (loadedSurface == nullptr)
	{
		printf("Failed to load image.  SDL_image error: %s\n", IMG_GetError());
		return false;
	}

	SDL_SetColorKey(loadedSurface, SDL_TRUE, SDL_MapRGB(loadedSurface->format, 0, 0xFF, 0xFF));
	texture = SDL_CreateTextureFromSurface(gRenderer, loadedSurface);
	if (texture == nullptr)
	{
		printf("Failed to create texture.  SDL error: %s\n", SDL_GetError());
		return false;
	}

	uint32_t x = 0;
	uint32_t y = 0;
	for (uint32_t i = 0; i < MAP_DATA_COUNT; i++)
	{
		if (x == 256)
		{
			x = 0;
			y = 16;
		}

		gTileClips[i].x = x;
		gTileClips[i].y = y;
		gTileClips[i].w = 16;
		gTileClips[i].h = 16;
		x += 16;
	}

	SDL_FreeSurface(loadedSurface);
	gTexture = texture;
	return (gTexture != nullptr);
}

static void close()
{
	if (gTexture != nullptr)
	{
		SDL_DestroyTexture(gTexture);
		gTexture = nullptr;
	}

	SDL_DestroyRenderer(gRenderer);
	gRenderer = nullptr;

	SDL_DestroyWindow(gWindow);
	gWindow = nullptr;

	IMG_Quit();
	SDL_Quit();
}

void loop_handler(void*)
{
	SDL_Event e;
	while (SDL_PollEvent(&e) != 0)
	{
		if (e.type == SDL_QUIT)
		{
			gQuit = true;
		}
		else if (e.type == SDL_KEYDOWN)
		{
			if (e.key.keysym.sym == SDLK_ESCAPE)
			{
				gQuit = true;
			}
			gGame.HandleEvent(true, e.key.keysym.sym);
		}
		else if (e.type == SDL_KEYUP)
		{
			gGame.HandleEvent(false, e.key.keysym.sym);
		}
	}

	gGame.Step();

	SDL_SetRenderDrawColor(gRenderer, 0xFF, 0xFF, 0xFF, 0xFF);
	SDL_RenderClear(gRenderer);

	gGame.Render();

	SDL_RenderPresent(gRenderer);
}

int main(int argc, char* args[])
{
	if (!init())
	{
		printf("Failed to initialize.\n");
		return EXIT_FAILURE;
	}

	if (!loadMedia())
	{
		printf("Failed to load media.\n");
		return EXIT_FAILURE;
	}

	unsigned int a = SDL_GetTicks();
	unsigned int b = SDL_GetTicks();
	double delta = 0;

	while (!gQuit)
	{
		a = SDL_GetTicks();
		delta = a - b;

		if (delta > 1000 / 30.0)
		{
			b = a;
			loop_handler(NULL);
		}
	}

	close();
	return 0;
}