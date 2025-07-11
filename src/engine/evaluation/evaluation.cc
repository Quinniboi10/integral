#include "evaluation.h"

#include "nnue/nnue.h"

namespace eval {

TUNABLE_STEP(kMaterialScaleBase, 26909, 10000, 32768, false, 500);

Score Evaluate(Board &board) {
  const auto network_eval = nnue::Evaluate(board);

#if DATAGEN
  return network_eval;
#endif

  const auto &state = board.GetState();
  const auto material_phase =
      *kSeePieceScores[kKnight] * state.Knights().PopCount() +
      *kSeePieceScores[kBishop] * state.Bishops().PopCount() +
      *kSeePieceScores[kRook] * state.Rooks().PopCount() +
      *kSeePieceScores[kQueen] * state.Queens().PopCount();

  return network_eval * (kMaterialScaleBase + material_phase) / 32768;
}

bool StaticExchange(Move move, int threshold, const BoardState &state) {
  const auto from = move.GetFrom();
  const auto to = move.GetTo();

  const PieceType &from_piece = state.GetPieceType(from);
  // Ignore en passant captures and castling
  if (move.IsEnPassant(state) ||
      (from_piece == kKing && std::abs(static_cast<int>(from) - to) == 2)) {
    return threshold <= 0;
  }

  // Score represents the maximum number of points the opponent can gain with
  // the next capture
  Score score = *kSeePieceScores[state.GetPieceType(to)] - threshold;
  // If the captured piece is worth less than what we can give up, we lose
  if (score < 0) {
    return false;
  }

  score = *kSeePieceScores[from_piece] - score;
  // If we captured a piece with equal/greater value than our capturing piece,
  // we win
  if (score <= 0) {
    return true;
  }

  const BitBoard &pawns = state.Pawns();
  const BitBoard &knights = state.Knights();
  const BitBoard &bishops = state.Bishops();
  const BitBoard &rooks = state.Rooks();
  const BitBoard &queens = state.Queens();
  const BitBoard &kings = state.Kings();

  BitBoard occupied = state.Occupied();
  occupied.ClearBit(from);
  occupied.ClearBit(to);

  // Get all pieces that attack the capture square
  auto pawn_attackers =
      (move_gen::PawnAttacks(to, Color::kWhite) & state.Pawns(Color::kBlack)) |
      (move_gen::PawnAttacks(to, Color::kBlack) & state.Pawns(Color::kWhite));
  auto knight_attackers = move_gen::KnightMoves(to) & state.Knights();

  BitBoard bishop_attacks = move_gen::BishopMoves(to, occupied);
  BitBoard rook_attacks = move_gen::RookMoves(to, occupied);

  const BitBoard bishop_attackers = bishop_attacks & bishops;
  const BitBoard rook_attackers = rook_attacks & rooks;
  const BitBoard queen_attackers = (bishop_attacks | rook_attacks) & queens;
  const BitBoard king_attackers = move_gen::KingAttacks(to) & kings;

  // Compute all attacking pieces for this square minus the captured and
  // capturing piece
  BitBoard all_attackers = pawn_attackers | knight_attackers |
                           bishop_attackers | rook_attackers | queen_attackers |
                           king_attackers;
  all_attackers &= occupied;

  Color turn = state.turn;
  Color winner = state.turn;
  
  const auto white_pinned = state.pinned[Color::kWhite] & state.Occupied(Color::kWhite);
  const auto black_pinned = state.pinned[Color::kBlack] & state.Occupied(Color::kBlack);
  
  const auto white_king_ray = move_gen::RayIntersecting(to, state.King(Color::kWhite).GetLsb());
  const auto black_king_ray = move_gen::RayIntersecting(to, state.King(Color::kBlack).GetLsb());

  const auto white_pinned_aligned = white_king_ray & white_pinned;
  const auto black_pinned_aligned = black_king_ray & black_pinned;

  const auto pinned = white_pinned | black_pinned;
  const auto pinned_aligned = white_pinned_aligned | black_pinned_aligned;

  // Loop through all pieces that attack the capture square
  while (true) {
    turn = FlipColor(turn);
    all_attackers &= occupied;

    BitBoard our_attackers = all_attackers & state.Occupied(turn);
    if ((state.pinned[turn] & occupied)) {
      our_attackers &= ~pinned | pinned_aligned;
    }

    // If the current side to move has no attackers left, they lose
    if (!our_attackers) {
      break;
    }

    // Without considering piece values, the winner of an exchange is whoever
    // has more attackers, therefore we set the winner's side to the current
    // side to move only after we check if they can attack
    winner = FlipColor(winner);

    // Find the least valuable attacker
    BitBoard next_attacker;
    int attacker_value;

    if ((next_attacker = our_attackers & pawns)) {
      attacker_value = *kSeePieceScores[kPawn];
      occupied.ClearBit(next_attacker.GetLsb());

      // Add pieces that were diagonal xray attacking the captured piece
      bishop_attacks = move_gen::BishopMoves(to, occupied);
      all_attackers |= bishop_attacks & (bishops | queens);
    } else if ((next_attacker = our_attackers & knights)) {
      attacker_value = *kSeePieceScores[kKnight];
      occupied.ClearBit(next_attacker.GetLsb());
    } else if ((next_attacker = our_attackers & bishops)) {
      attacker_value = *kSeePieceScores[kBishop];
      occupied.ClearBit(next_attacker.GetLsb());

      // Add pieces that were xray attacking the captured piece
      bishop_attacks = move_gen::BishopMoves(to, occupied);
      all_attackers |= bishop_attacks & (bishops | queens);
    } else if ((next_attacker = our_attackers & rooks)) {
      attacker_value = *kSeePieceScores[kRook];
      occupied.ClearBit(next_attacker.GetLsb());

      // Add pieces that were xray attacking the captured piece
      rook_attacks = move_gen::RookMoves(to, occupied);
      all_attackers |= rook_attacks & (rooks | queens);
    } else if ((next_attacker = our_attackers & queens)) {
      attacker_value = *kSeePieceScores[kQueen];
      occupied.ClearBit(next_attacker.GetLsb());

      // Add pieces that were xray attacking the captured piece
      rook_attacks = move_gen::RookMoves(to, occupied);
      bishop_attacks = move_gen::BishopMoves(to, occupied);
      all_attackers |= (rook_attacks & (queens | rooks)) |
                       (bishop_attacks & (queens | bishops));
    } else {
      // King: check if we capture a piece that our opponent is still
      // attacking
      return (all_attackers & state.Occupied(FlipColor(turn)))
               ? state.turn != winner
               : state.turn == winner;
    }

    // Score represents how many points the other side can gain after this
    // capture. If initially a knight captured a queen, the other side can
    // gain 3 - 9 = -6 points. If we flip it and initially a queen captured a
    // knight, the other side can gain 9 - 3 = 6 points
    score = -score + 1 + attacker_value;
    // Quit early if the exchange is lost or neutral
    if (score <= 0) {
      break;
    }
  }

  return state.turn == winner;
}

}  // namespace eval