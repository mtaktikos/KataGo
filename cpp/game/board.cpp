#include "../game/board.h"

/*
 * board.cpp
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modificationss).
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "../core/rand.h"

using namespace std;

//STATIC VARS-----------------------------------------------------------------------------
bool Board::IS_ZOBRIST_INITALIZED = false;
Hash128 Board::ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][NUM_BOARD_COLORS];
Hash128 Board::ZOBRIST_PLAYER_HASH[4];
Hash128 Board::ZOBRIST_STAGENUM_HASH[STAGE_NUM_EACH_PLA];
Hash128 Board::ZOBRIST_STAGELOC_HASH[MAX_ARR_SIZE][STAGE_NUM_EACH_PLA];
Hash128 Board::ZOBRIST_NEXTPLA_HASH[4];
const Hash128 Board::ZOBRIST_GAME_IS_OVER = //Based on sha256 hash of Board::ZOBRIST_GAME_IS_OVER
  Hash128(0xb6f9e465597a77eeULL, 0xf1d583d960a4ce7fULL);


const int32_t Board::priorityConvWeights[3 * 5 * 5] =
{
  2,2,2,2,2,
  2,2,2,2,2,
  2,2,1,2,2,
  2,2,2,2,2,
  2,2,2,2,2,//empty

   71, 173, 281, 173,  71,
  173, 409, 541, 409, 173,
  281, 541,   1, 541, 281,
  173, 409, 541, 409, 173,
   71, 173, 281, 173,  71,//nextpla

   29, 113, 229, 113,  29,
  113, 349, 463, 349, 113,
  229, 463,   1, 463, 229,
  113, 349, 463, 349, 113,
   29, 113, 229, 113,  29 //opp

};
//max=6953 < 2^11

//LOCATION--------------------------------------------------------------------------------
Loc Location::getLoc(int x, int y, int x_size)
{
  return (x+1) + (y+1)*(x_size+1);
}
int Location::getX(Loc loc, int x_size)
{
  return (loc % (x_size+1)) - 1;
}
int Location::getY(Loc loc, int x_size)
{
  return (loc / (x_size+1)) - 1;
}
void Location::getAdjacentOffsets(short adj_offsets[8], int x_size)
{
  adj_offsets[0] = -(x_size+1);
  adj_offsets[1] = -1;
  adj_offsets[2] = 1;
  adj_offsets[3] = (x_size+1);
  adj_offsets[4] = -(x_size+1)-1;
  adj_offsets[5] = -(x_size+1)+1;
  adj_offsets[6] = (x_size+1)-1;
  adj_offsets[7] = (x_size+1)+1;
}

bool Location::isAdjacent(Loc loc0, Loc loc1, int x_size)
{
  return loc0 == loc1 - (x_size+1) || loc0 == loc1 - 1 || loc0 == loc1 + 1 || loc0 == loc1 + (x_size+1);
}

Loc Location::getMirrorLoc(Loc loc, int x_size, int y_size) {
  if(loc == Board::NULL_LOC || loc == Board::PASS_LOC)
    return loc;
  return getLoc(x_size-1-getX(loc,x_size),y_size-1-getY(loc,x_size),x_size);
}

Loc Location::getCenterLoc(int x_size, int y_size) {
  if(x_size % 2 == 0 || y_size % 2 == 0)
    return Board::NULL_LOC;
  return getLoc(x_size / 2, y_size / 2, x_size);
}

Loc Location::getCenterLoc(const Board& b) {
  return getCenterLoc(b.x_size,b.y_size);
}

bool Location::isCentral(Loc loc, int x_size, int y_size) {
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  return x >= (x_size-1)/2 && x <= x_size/2 && y >= (y_size-1)/2 && y <= y_size/2;
}

bool Location::isNearCentral(Loc loc, int x_size, int y_size) {
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  return x >= (x_size-1)/2-1 && x <= x_size/2+1 && y >= (y_size-1)/2-1 && y <= y_size/2+1;
}


#define FOREACHADJ(BLOCK) {int ADJOFFSET = -(x_size+1); {BLOCK}; ADJOFFSET = -1; {BLOCK}; ADJOFFSET = 1; {BLOCK}; ADJOFFSET = x_size+1; {BLOCK}};
#define ADJ0 (-(x_size+1))
#define ADJ1 (-1)
#define ADJ2 (1)
#define ADJ3 (x_size+1)

//CONSTRUCTORS AND INITIALIZATION----------------------------------------------------------

Board::Board()
{
  init(DEFAULT_LEN,DEFAULT_LEN);
}

Board::Board(int x, int y)
{
  init(x,y);
}


Board::Board(const Board& other)
{
  x_size = other.x_size;
  y_size = other.y_size;

  memcpy(colors, other.colors, sizeof(Color)*MAX_ARR_SIZE);

  pos_hash = other.pos_hash;
  memcpy(adj_offsets, other.adj_offsets, sizeof(short)*8);

  nextPla = other.nextPla;
  stage = other.stage;
  memcpy(midLocs, other.midLocs, sizeof(Loc) * STAGE_NUM_EACH_PLA);
  lastMovePriority = other.lastMovePriority;
}

void Board::init(int xS, int yS)
{
  assert(IS_ZOBRIST_INITALIZED);
  if(xS < 0 || yS < 0 || xS > MAX_LEN || yS > MAX_LEN)
    throw StringError("Board::init - invalid board size");

  x_size = xS;
  y_size = yS;

  for(int i = 0; i < MAX_ARR_SIZE; i++)
    colors[i] = C_WALL;

  for(int y = 0; y < y_size; y++)
  {
    for(int x = 0; x < x_size; x++)
    {
      Loc loc = (x+1) + (y+1)*(x_size+1);
      colors[loc] = C_EMPTY;
      // empty_list.add(loc);
    }
  }

  //只有第一步的时候，midLocs[0]==NULL_LOC && stage==1
  for (int i = 0; i < STAGE_NUM_EACH_PLA; i++)
  {
    midLocs[i] = Board::NULL_LOC;
  }
  nextPla = C_BLACK;

  //黑棋第一步只能走一个子，所以stage=1
  stage = 1;
  lastMovePriority = MAX_MOVE_PRIORITY;

  pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size] ^ ZOBRIST_NEXTPLA_HASH[nextPla] ^ ZOBRIST_STAGENUM_HASH[stage] ^ ZOBRIST_STAGELOC_HASH[midLocs[0]][0];



  Location::getAdjacentOffsets(adj_offsets,x_size);


}

int32_t Board::getMovePriority(Color pla, Loc loc) const
{
  if (loc == NULL_LOC)//only first move
    return MAX_MOVE_PRIORITY;
  if (!isOnBoard(loc) || colors[loc] != C_EMPTY)
    return 0;
  //if (stage == 1 && midLocs[0] == loc)
  //  return 0;

  Color opp = getOpp(pla);

  //卷积
  //棋子多的地方权值高
  //至少为1
  int32_t convTotal = 0;
  int x0 = Location::getX(loc, x_size);
  int y0 = Location::getY(loc, x_size);
  for (int dy = -2; dy <= 2; dy++)
  {
    int y = y0 + dy;
    if (y < 0)continue;
    if (y >= y_size)break;
    for (int dx = -2; dx <= 2; dx++)
    {
      int x = x0 + dx;
      if (x < 0)continue;
      if (x >= x_size)break;

      Loc loc1 = Location::getLoc(x, y, x_size);
      Color color = colors[loc1];
      if (color == pla)
        convTotal += priorityConvWeights[5 * 5 * 1 + 5 * (dy+2) + dx+2];
      else if (color == opp)
        convTotal += priorityConvWeights[5 * 5 * 2 + 5 * (dy+2) + dx+2];
      else if (color == C_EMPTY)
        convTotal += priorityConvWeights[5 * 5 * 0 + 5 * (dy+2) + dx+2];
      else ASSERT_UNREACHABLE;
    }
  }


  return convTotal;
}

void Board::initHash()
{
  if(IS_ZOBRIST_INITALIZED)
    return;
  Rand rand("Board::initHash()");

  auto nextHash = [&rand]() {
    uint64_t h0 = rand.nextUInt64();
    uint64_t h1 = rand.nextUInt64();
    return Hash128(h0,h1);
  };

  for(int i = 0; i<4; i++)
    ZOBRIST_PLAYER_HASH[i] = nextHash();

  //afffected by the size of the board we compile with.
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    for(Color j = 0; j<NUM_BOARD_COLORS; j++) {
      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_BOARD_HASH[i][j] = Hash128();
      else
        ZOBRIST_BOARD_HASH[i][j] = nextHash();

    }
  }

  for (int i = 0; i < STAGE_NUM_EACH_PLA; i++)
  {
    ZOBRIST_STAGENUM_HASH[i] = nextHash();
    for(int j=0;j<MAX_ARR_SIZE;j++)
      ZOBRIST_STAGELOC_HASH[j][i] = nextHash();
    ZOBRIST_STAGELOC_HASH[Board::NULL_LOC][i] = Hash128();
  }

  for (Color j = 0; j < 4; j++) {
    ZOBRIST_NEXTPLA_HASH[j] = nextHash();
  }
  //Reseed the random number generator so that these size hashes are also
  //not affected by the size of the board we compile with
  rand.init("Board::initHash() for ZOBRIST_SIZE hashes");
  for(int i = 0; i<MAX_LEN+1; i++) {
    ZOBRIST_SIZE_X_HASH[i] = nextHash();
    ZOBRIST_SIZE_Y_HASH[i] = nextHash();
  }


  IS_ZOBRIST_INITALIZED = true;
}

Hash128 Board::getSitHash(Player pla) const {
  Hash128 h = pos_hash;
  h ^= Board::ZOBRIST_PLAYER_HASH[pla];
  return h;
}

int Board::getMaxConnectLengthAndWinLoc(Color pla, Loc& bestLoc) const
{
  int maxConLen = 0;
  int maxPriority = 0;//连四取最大，连五取最小
  bestLoc = NULL_LOC;

  for(int y0=0;y0<y_size;y0++)
    for (int x0 = 0; x0 < x_size; x0++)
    {
      Loc loc0 = Location::getLoc(x0, y0, x_size);
      for (int dir = 0; dir < 4; dir++)
      {
        short adj = adj_offsets[dir * 2];
        int emptyNum = 0;
        Loc emptyLocs[2] = { NULL_LOC,NULL_LOC };
        for (int len = 0; len < 6; len++)
        {
          Loc loc = loc0 + len * adj;
          Color color = colors[loc];
          if (color == pla||loc==midLocs[0])continue;
          else if (color == C_EMPTY)
          {
            if (emptyNum >= 2 || ((stage==1||maxConLen==5) && emptyNum >= 1))
            {
              emptyNum = 3;
              break;
            }
            emptyLocs[emptyNum] = loc;
            emptyNum++;
          }
          else
          {
            emptyNum = 3;
            break;
          }
        }

        if (emptyNum > 2)continue;//nothing
        else if (emptyNum == 2)//four
        {
          if (maxConLen <= 4)
          {
            maxConLen = 4;
            for (int i = 0; i < 2; i++)
            {
              Loc emptyLoc = emptyLocs[i];
              int32_t priority=getMovePriority(pla, emptyLoc);
              if (priority > maxPriority)
              {
                maxPriority = priority;
                bestLoc = emptyLoc;
              }
            }
          }
          else continue;
        }
        else if (emptyNum == 1)//five
        {
          if (maxConLen < 5)
          {
            maxConLen = 5;
            Loc emptyLoc = emptyLocs[0];
            int32_t priority=getMovePriority(pla, emptyLoc);
            maxPriority = priority;
            bestLoc = emptyLoc;
          }
          else if (maxConLen == 5)
          {
            Loc emptyLoc = emptyLocs[0];
            int32_t priority=getMovePriority(pla, emptyLoc);
            if (priority < lastMovePriority)continue;//illegal
            if (priority < maxPriority)
            {
              maxPriority = priority;
              bestLoc = emptyLoc;
            }
          }
          else continue;
        }
        else if (emptyNum == 0)//win
        {
          bestLoc = NULL_LOC;
          return 6;
        }
      }
    }
  return maxConLen;
}

bool Board::isOnBoard(Loc loc) const {
  return loc >= 0 && loc < MAX_ARR_SIZE && colors[loc] != C_WALL;
}

//Check if moving here is illegal.
bool Board::isLegal(Loc loc, Player pla, bool isMultiStoneSuicideLegal) const
{
  (void)isMultiStoneSuicideLegal;

  if (pla != nextPla)
  {
    std::cout << "Error next player ";
    return false;
  }
  if (loc == PASS_LOC)//pass直接判负，但是不作为“illegal move”
    return true;

  if (!isOnBoard(loc))
    return false;

  if (stage == 0)//第一步
  {
    return colors[loc] == C_EMPTY;
  }
  else if (stage == 1)//第二步
  {
    return colors[loc] == C_EMPTY &&
      midLocs[0] != loc &&
      getMovePriority(pla, loc) <= lastMovePriority;
  }

  ASSERT_UNREACHABLE;
  return false;
}

bool Board::isEmpty() const {
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] != C_EMPTY)
        return false;
    }
  }
  return true;
}

int Board::numStonesOnBoard() const {
  int num = 0;
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] == C_BLACK || colors[loc] == C_WHITE)
        num += 1;
    }
  }
  return num;
}

int Board::numPlaStonesOnBoard(Player pla) const {
  int num = 0;
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] == pla)
        num += 1;
    }
  }
  return num;
}

bool Board::setStone(Loc loc, Color color)
{
  if(loc < 0 || loc >= MAX_ARR_SIZE || colors[loc] == C_WALL)
    return false;
  if(color ==C_WALL)
    return false;

  Color oldColor = colors[loc];
  colors[loc] = color;
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][oldColor];
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][color];
  return true;
}


//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool Board::playMove(Loc loc, Player pla, bool isMultiStoneSuicideLegal)
{
  if(isLegal(loc,pla,isMultiStoneSuicideLegal))
  {
    playMoveAssumeLegal(loc,pla);
    return true;
  }
  return false;
}

Hash128 Board::getPosHashAfterMove(Loc loc, Player pla) const {
  if(loc == PASS_LOC)
    return pos_hash;
  assert(loc != NULL_LOC);

  Hash128 hash = pos_hash;
  hash ^= ZOBRIST_BOARD_HASH[loc][pla];


  return hash;
}

//Plays the specified move, assuming it is legal.
void Board::playMoveAssumeLegal(Loc loc, Player pla)
{
  if (pla != nextPla)
  {
    std::cout << "Error next player ";
  }
  if (stage == 0)//第一步
  {
    lastMovePriority = getMovePriority(pla, loc);

    stage = 1;
    pos_hash ^= ZOBRIST_STAGENUM_HASH[0];
    pos_hash ^= ZOBRIST_STAGENUM_HASH[1];

    midLocs[0] = loc;
    pos_hash ^= ZOBRIST_STAGELOC_HASH[loc][0];

  }
  else if (stage == 1)//第二步
  {
    lastMovePriority = MAX_MOVE_PRIORITY;
    if (isOnBoard(loc))
      setStone(loc, pla);
    Loc loc1 = midLocs[0];
    if (isOnBoard(loc1))
      setStone(loc1, pla);

    stage = 0;
    pos_hash ^= ZOBRIST_STAGENUM_HASH[1];
    pos_hash ^= ZOBRIST_STAGENUM_HASH[0];
    pos_hash ^= ZOBRIST_STAGELOC_HASH[midLocs[0]][0];
    midLocs[0] = Board::NULL_LOC;

    nextPla = getOpp(nextPla);
    pos_hash ^= ZOBRIST_NEXTPLA_HASH[getOpp(nextPla)];
    pos_hash ^= ZOBRIST_NEXTPLA_HASH[nextPla];
  }
  else ASSERT_UNREACHABLE;
}

Player Board::nextnextPla() const
{
  if (stage == STAGE_NUM_EACH_PLA - 1)
    return getOpp(nextPla);
  else return nextPla;
}



int Location::distance(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
}

int Location::euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return dx*dx + dy*dy;
}

//TACTICAL STUFF--------------------------------------------------------------------


void Board::checkConsistency() const {
  const string errLabel = string("Board::checkConsistency(): ");


  vector<Loc> buf;
  Hash128 tmp_pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];
  for(Loc loc = 0; loc < MAX_ARR_SIZE; loc++) {
    int x = Location::getX(loc,x_size);
    int y = Location::getY(loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size) {
      if(colors[loc] != C_WALL)
        throw StringError(errLabel + "Non-WALL value outside of board legal area");
    }
    else { 
      if(colors[loc] == C_EMPTY) {
      // if(!empty_list.contains(loc))
      //   throw StringError(errLabel + "Empty list doesn't contain empty location");
      }
      else if(colors[loc] !=C_WALL) {
        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][C_EMPTY];
      }
      else
        throw StringError(errLabel + "Non-(black,white,empty) value within board legal area");
    }
  }

  tmp_pos_hash ^= ZOBRIST_NEXTPLA_HASH[nextPla];
  tmp_pos_hash ^= ZOBRIST_STAGENUM_HASH[stage];
  for (int i = 0; i < STAGE_NUM_EACH_PLA; i++)
  {
    //std::cout << ZOBRIST_STAGELOC_HASH[midLocs[i]][i]<<" ";
    tmp_pos_hash ^= ZOBRIST_STAGELOC_HASH[midLocs[i]][i];
  }


  if (pos_hash != tmp_pos_hash)
  {
    std::cout << "Stage=" << stage << ",NextPla=" << int(nextPla) << std::endl;
    throw StringError(errLabel + "Pos hash does not match expected");
  }

  if (stage == 1 && midLocs[0] == NULL_LOC)
  {
    if (numStonesOnBoard() != 0)
    {
      throw StringError(errLabel + "midLocs[0] == NULL_LOC when not first move");
    }
  }

  if (stage == 1)
  {
    if (getMovePriority(nextPla, midLocs[0]) != lastMovePriority)
    {
      cout << getMovePriority(nextPla, midLocs[0]) << " " << lastMovePriority << endl;
      throw StringError(errLabel + "lastMovePriority not match");
    }
  }

  short tmpAdjOffsets[8];
  Location::getAdjacentOffsets(tmpAdjOffsets,x_size);
  for(int i = 0; i<8; i++)
    if(tmpAdjOffsets[i] != adj_offsets[i])
      throw StringError(errLabel + "Corrupted adj_offsets array");
}

bool Board::isEqualForTesting(const Board& other, bool checkNumCaptures, bool checkSimpleKo) const {
  (void)checkNumCaptures;
  (void)checkSimpleKo;
  checkConsistency();
  other.checkConsistency();
  if(x_size != other.x_size)
    return false;
  if(y_size != other.y_size)
    return false;
  if(pos_hash != other.pos_hash)
    return false;
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    if(colors[i] != other.colors[i])
      return false;
  }
  //We don't require that the chain linked lists are in the same order.
  //Consistency check ensures that all the linked lists are consistent with colors array, which we checked.
  return true;
}



//IO FUNCS------------------------------------------------------------------------------------------

char PlayerIO::colorToChar(Color c)
{
  switch(c) {
  case C_BLACK: return 'X';
  case C_WHITE: return 'O';
  case C_EMPTY: return '.';
  case C_BANLOC: return 'B';
  default:  return '#';
  }
}

string PlayerIO::playerToString(Color c)
{
  switch(c) {
  case C_BLACK: return "Black";
  case C_WHITE: return "White";
  case C_EMPTY: return "Empty";
  default:  return "Wall";
  }
}

string PlayerIO::playerToStringShort(Color c)
{
  switch(c) {
  case C_BLACK: return "B";
  case C_WHITE: return "W";
  case C_EMPTY: return "E";
  default:  return "";
  }
}

bool PlayerIO::tryParsePlayer(const string& s, Player& pla) {
  string str = Global::toLower(s);
  if(str == "black" || str == "b") {
    pla = P_BLACK;
    return true;
  }
  else if(str == "white" || str == "w") {
    pla = P_WHITE;
    return true;
  }
  return false;
}

Player PlayerIO::parsePlayer(const string& s) {
  Player pla = C_EMPTY;
  bool suc = tryParsePlayer(s,pla);
  if(!suc)
    throw StringError("Could not parse player: " + s);
  return pla;
}

string Location::toStringMach(Loc loc, int x_size)
{
  if(loc == Board::PASS_LOC)
    return string("pass");
  if(loc == Board::NULL_LOC)
    return string("null");
  char buf[128];
  sprintf(buf,"(%d,%d)",getX(loc,x_size),getY(loc,x_size));
  return string(buf);
}

string Location::toString(Loc loc, int x_size, int y_size)
{
  if(x_size > 25*25)
    return toStringMach(loc,x_size);
  if(loc == Board::PASS_LOC)
    return string("pass");
  if(loc == Board::NULL_LOC)
    return string("null");
  const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  if(x >= x_size || x < 0 || y < 0 || y >= y_size)
    return toStringMach(loc,x_size);

  char buf[128];
  if(x <= 24)
    sprintf(buf,"%c%d",xChar[x],y_size-y);
  else
    sprintf(buf,"%c%c%d",xChar[x/25-1],xChar[x%25],y_size-y);
  return string(buf);
}

string Location::toString(Loc loc, const Board& b) {
  return toString(loc,b.x_size,b.y_size);
}

string Location::toStringMach(Loc loc, const Board& b) {
  return toStringMach(loc,b.x_size);
}

static bool tryParseLetterCoordinate(char c, int& x) {
  if(c >= 'A' && c <= 'H')
    x = c-'A';
  else if(c >= 'a' && c <= 'h')
    x = c-'a';
  else if(c >= 'J' && c <= 'Z')
    x = c-'A'-1;
  else if(c >= 'j' && c <= 'z')
    x = c-'a'-1;
  else
    return false;
  return true;
}

bool Location::tryOfString(const string& str, int x_size, int y_size, Loc& result) {
  string s = Global::trim(str);
  if(s.length() < 2)
    return false;
  if(Global::isEqualCaseInsensitive(s,string("pass")) || Global::isEqualCaseInsensitive(s,string("pss"))) {
    result = Board::PASS_LOC;
    return true;
  }
  if(s[0] == '(') {
    if(s[s.length()-1] != ')')
      return false;
    s = s.substr(1,s.length()-2);
    vector<string> pieces = Global::split(s,',');
    if(pieces.size() != 2)
      return false;
    int x;
    int y;
    bool sucX = Global::tryStringToInt(pieces[0],x);
    bool sucY = Global::tryStringToInt(pieces[1],y);
    if(!sucX || !sucY)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
  else {
    int x;
    if(!tryParseLetterCoordinate(s[0],x))
      return false;

    //Extended format
    if((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) {
      int x1;
      if(!tryParseLetterCoordinate(s[1],x1))
        return false;
      x = (x+1) * 25 + x1;
      s = s.substr(2,s.length()-2);
    }
    else {
      s = s.substr(1,s.length()-1);
    }

    int y;
    bool sucY = Global::tryStringToInt(s,y);
    if(!sucY)
      return false;
    y = y_size - y;
    if(x < 0 || y < 0 || x >= x_size || y >= y_size)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
}

bool Location::tryOfStringAllowNull(const string& str, int x_size, int y_size, Loc& result) {
  if(str == "null") {
    result = Board::NULL_LOC;
    return true;
  }
  return tryOfString(str, x_size, y_size, result);
}

bool Location::tryOfString(const string& str, const Board& b, Loc& result) {
  return tryOfString(str,b.x_size,b.y_size,result);
}

bool Location::tryOfStringAllowNull(const string& str, const Board& b, Loc& result) {
  return tryOfStringAllowNull(str,b.x_size,b.y_size,result);
}

Loc Location::ofString(const string& str, int x_size, int y_size) {
  Loc result;
  if(tryOfString(str,x_size,y_size,result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofStringAllowNull(const string& str, int x_size, int y_size) {
  Loc result;
  if(tryOfStringAllowNull(str,x_size,y_size,result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofString(const string& str, const Board& b) {
  return ofString(str,b.x_size,b.y_size);
}


Loc Location::ofStringAllowNull(const string& str, const Board& b) {
  return ofStringAllowNull(str,b.x_size,b.y_size);
}

vector<Loc> Location::parseSequence(const string& str, const Board& board) {
  vector<string> pieces = Global::split(Global::trim(str),' ');
  vector<Loc> locs;
  for(size_t i = 0; i<pieces.size(); i++) {
    string piece = Global::trim(pieces[i]);
    if(piece.length() <= 0)
      continue;
    locs.push_back(Location::ofString(piece,board));
  }
  return locs;
}

void Board::printBoard(ostream& out, const Board& board, Loc markLoc, const vector<Move>* hist) {
  if(hist != NULL)
    out << "MoveNum: " << hist->size() << " ";
  out << "HASH: " << board.pos_hash << "\n";
  bool showCoords = board.x_size <= 50 && board.y_size <= 50;
  if(showCoords) {
    const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    out << "  ";
    for(int x = 0; x < board.x_size; x++) {
      if(x <= 24) {
        out << " ";
        out << xChar[x];
      }
      else {
        out << "A" << xChar[x-25];
      }
    }
    out << "\n";
  }

  for(int y = 0; y < board.y_size; y++)
  {
    if(showCoords) {
      char buf[16];
      sprintf(buf,"%2d",board.y_size-y);
      out << buf << ' ';
    }
    for(int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x,y,board.x_size);
      char s = PlayerIO::colorToChar(board.colors[loc]);
      if(board.colors[loc] == C_EMPTY && markLoc == loc)
        out << '@';
      else
        out << s;

      bool histMarked = false;
      if(hist != NULL) {
        size_t start = hist->size() >= 3 ? hist->size()-3 : 0;
        for(size_t i = 0; start+i < hist->size(); i++) {
          if((*hist)[start+i].loc == loc) {
            out << (1+i);
            histMarked = true;
            break;
          }
        }
      }

      if(x < board.x_size-1 && !histMarked)
        out << ' ';
    }
    out << "\n";
  }
  out << "\n";
}

ostream& operator<<(ostream& out, const Board& board) {
  Board::printBoard(out,board,Board::NULL_LOC,NULL);
  return out;
}


string Board::toStringSimple(const Board& board, char lineDelimiter) {
  string s;
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x < board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      s += PlayerIO::colorToChar(board.colors[loc]);
    }
    s += lineDelimiter;
  }
  return s;
}

Board Board::parseBoard(int xSize, int ySize, const string& s) {
  return parseBoard(xSize,ySize,s,'\n');
}

Board Board::parseBoard(int xSize, int ySize, const string& s, char lineDelimiter) {
  Board board(xSize,ySize);
  vector<string> lines = Global::split(Global::trim(s),lineDelimiter);

  //Throw away coordinate labels line if it exists
  if(lines.size() == ySize+1 && Global::isPrefix(lines[0],"A"))
    lines.erase(lines.begin());

  if(lines.size() != ySize)
    throw StringError("Board::parseBoard - string has different number of board rows than ySize");

  for(int y = 0; y<ySize; y++) {
    string line = Global::trim(lines[y]);
    //Throw away coordinates if they exist
    size_t firstNonDigitIdx = 0;
    while(firstNonDigitIdx < line.length() && Global::isDigit(line[firstNonDigitIdx]))
      firstNonDigitIdx++;
    line.erase(0,firstNonDigitIdx);
    line = Global::trim(line);

    if(line.length() != xSize && line.length() != 2*xSize-1)
      throw StringError("Board::parseBoard - line length not compatible with xSize");

    for(int x = 0; x<xSize; x++) {
      char c;
      if(line.length() == xSize)
        c = line[x];
      else
        c = line[x*2];

      Loc loc = Location::getLoc(x,y,board.x_size);
      if(c == '.' || c == ' ' || c == '*' || c == ',' || c == '`')
        continue;
      else if(c == 'o' || c == 'O')
        board.setStone(loc,P_WHITE);
      else if(c == 'x' || c == 'X')
        board.setStone(loc,P_BLACK);
      else
        throw StringError(string("Board::parseBoard - could not parse board character: ") + c);
    }
  }
  return board;
}

nlohmann::json Board::toJson(const Board& board) {
  nlohmann::json data;
  data["xSize"] = board.x_size;
  data["ySize"] = board.y_size;
  data["stones"] = Board::toStringSimple(board,'|');
  return data;
}

Board Board::ofJson(const nlohmann::json& data) {
  int xSize = data["xSize"].get<int>();
  int ySize = data["ySize"].get<int>();
  Board board = Board::parseBoard(xSize,ySize,data["stones"].get<string>(),'|');
  return board;
}

