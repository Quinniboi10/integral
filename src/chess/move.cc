#include "move.h"
#include "../engine/uci/uci.h"

#include "board.h"

Move Move::NullMove() {
  return Move(0, 0, MoveType::kNormal);
}

Move::operator bool() const {
  return !IsNull();
}

Move Move::FromStr(std::string_view str, const BoardState &state) {
  constexpr int kMinMoveLen = 4, kMaxMoveLen = 5;
  if (str.length() < kMinMoveLen || str.length() > kMaxMoveLen)
    return Move::NullMove();

  const int from_rank = str[1] - '1', from_file = str[0] - 'a';
  const int to_rank = str[3] - '1', to_file = str[2] - 'a';

  if (from_rank < 0 || from_rank >= 8 || to_rank < 0 || to_rank >= 8 ||
      from_file < 0 || from_file >= 8 || to_file < 0 || to_file >= 8)
    return Move::NullMove();

  const auto from = Square::FromRankFile(from_rank, from_file);
  auto to = Square::FromRankFile(to_rank, to_file);

  auto flag = MoveType::kNormal;

  if (str.length() < kMaxMoveLen) {
    const auto piece = state.GetPieceType(from);
    if (piece == PieceType::kKing) {
      if (!uci::listener.GetOption("UCI_Chess960").GetValue<bool>() &&
          ((from == kE1 && to == kG1 && state.castle_rights.CanKingsideCastle(kWhite)) ||
           (from == kE1 && to == kC1 && state.castle_rights.CanQueensideCastle(kWhite)) ||
           (from == kE8 && to == kG8 && state.castle_rights.CanKingsideCastle(kBlack)) ||
           (from == kE8 && to == kC8 && state.castle_rights.CanQueensideCastle(kBlack)))) {
        const CastleSide side = to > from ? kKingside : kQueenside;
        to = state.castle_rights.CastleSq(state.turn, side);
        flag = MoveType::kCastle;
      } else if (uci::listener.GetOption("UCI_Chess960").GetValue<bool>() &&
                 (state.Rooks(state.turn) & (1ULL << to)).AsU64() > 0)
        flag = MoveType::kCastle;
    } else if (piece == PieceType::kPawn) {
      if (state.en_passant && to == state.en_passant) {
        flag = MoveType::kEnPassant;
      }
    }

    return Move(from, to, flag);
  }

  PromotionType promotion_type;
  switch (str[4]) {
    case 'q':
    case 'Q':
      promotion_type = PromotionType::kQueen;
      break;
    case 'r':
    case 'R':
      promotion_type = PromotionType::kRook;
      break;
    case 'b':
    case 'B':
      promotion_type = PromotionType::kBishop;
      break;
    case 'n':
    case 'N':
      promotion_type = PromotionType::kKnight;
      break;
    default:
      return Move::NullMove();
  }

  return Move(from, to, promotion_type);
}

bool Move::IsCapture(const BoardState &state) const {
  return (state.GetPieceType(GetTo()) != PieceType::kNone && GetType() != MoveType::kCastle) || IsEnPassant(state);
}

bool Move::IsNoisy(const BoardState &state) const {
  return IsCapture(state) || GetType() == MoveType::kPromotion;
}

bool Move::IsEnPassant(const BoardState &state) const {
  return GetType() == MoveType::kEnPassant;
}

bool Move::IsUnderPromotion() const {
  const auto promo_type = GetPromotionType();
  return GetType() == MoveType::kPromotion &&
         promo_type != PromotionType::kQueen &&
         promo_type != PromotionType::kKnight;
}

std::string Move::ToString() const {
  if (data_ == 0) return "null";

  std::string res = GetFrom().ToString();
  if (GetType() == MoveType::kCastle &&
      !uci::listener.GetOption("UCI_Chess960").GetValue<bool>()) {
    const bool isKingside = GetFrom() < GetTo();
    return res + (GetFrom() + (isKingside ? 2 : -2)).ToString();
  }

  res += GetTo().ToString();

  if (GetType() == MoveType::kPromotion) {
    switch (GetPromotionType()) {
      case PromotionType::kQueen:
        res += 'q';
        break;
      case PromotionType::kKnight:
        res += 'n';
        break;
      case PromotionType::kBishop:
        res += 'b';
        break;
      case PromotionType::kRook:
        res += 'r';
        break;
      default:
        break;
    }
  }

  return res;
}
