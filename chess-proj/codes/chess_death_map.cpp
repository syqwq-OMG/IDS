#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <iomanip>
#include <cmath>

#include "chess.hpp"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "../dataset/lichess_db_standard_rated_2025-11.pgn";
// const std::string PGN_FILE_PATH = "../dataset/catcatX.pgn";
const std::string OUTCOME_DIR = "../output/chess_death_map";
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/death_heatmap.json";
// ===========================================

using HeatmapArray = std::array<int, 64>;

// --- 1. 引入 Tracker (复用 chesskiller 的逻辑) ---
struct TrackedPiece {
    std::string id;       // 例如 "White_Pawn_a"
    bool promoted = false;
    bool active = false;
};

class AdvancedTracker {
public:
    std::array<TrackedPiece, 64> board_map;

    AdvancedTracker() { initialize_ids(); }

    void initialize_ids() {
        for(auto& p : board_map) p = { "", false, false };
        
        const std::vector<char> files = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
        auto set = [&](chess::Square sq, std::string id) {
            board_map[sq.index()] = {id, false, true};
        };

        // White Init
        set(chess::Square::SQ_A1, "White_Rook_a");
        set(chess::Square::SQ_B1, "White_Knight_b");
        set(chess::Square::SQ_C1, "White_Bishop_c");
        set(chess::Square::SQ_D1, "White_Queen");
        set(chess::Square::SQ_E1, "White_King");
        set(chess::Square::SQ_F1, "White_Bishop_f");
        set(chess::Square::SQ_G1, "White_Knight_g");
        set(chess::Square::SQ_H1, "White_Rook_h");
        for (int i = 0; i < 8; ++i) {
            set(chess::Square(chess::File(i), chess::Rank::RANK_2), "White_Pawn_" + std::string(1, files[i]));
        }

        // Black Init
        set(chess::Square::SQ_A8, "Black_Rook_a");
        set(chess::Square::SQ_B8, "Black_Knight_b");
        set(chess::Square::SQ_C8, "Black_Bishop_c");
        set(chess::Square::SQ_D8, "Black_Queen");
        set(chess::Square::SQ_E8, "Black_King");
        set(chess::Square::SQ_F8, "Black_Bishop_f");
        set(chess::Square::SQ_G8, "Black_Knight_g");
        set(chess::Square::SQ_H8, "Black_Rook_h");
        for (int i = 0; i < 8; ++i) {
            set(chess::Square(chess::File(i), chess::Rank::RANK_7), "Black_Pawn_" + std::string(1, files[i]));
        }
    }

    // 获取纯净 ID (不带 _promoted 后缀，方便归类)
    // 如果你希望区分升变后的兵，可以使用 p.id + (p.promoted ? "_promoted" : "")
    std::string get_id(int sq_idx) {
        if (!board_map[sq_idx].active) return "";
        return board_map[sq_idx].id; 
    }
};

// --- 2. 统计结构 ---
struct DeathStats {
    // 汇总
    HeatmapArray total_deaths = {0};
    HeatmapArray white_total_deaths = {0};
    HeatmapArray black_total_deaths = {0};

    // 细分 (Key 现在是 "White_Pawn_a", "Black_King" 等)
    std::map<std::string, HeatmapArray> detailed_deaths;

    int games_processed = 0;
    long long total_death_count = 0;

    void commit_death(const std::string& full_id, int square_idx, bool is_white) {
        if (square_idx < 0 || square_idx >= 64) return;
        if (full_id.empty()) return;

        total_death_count++;

        // 1. 更新总榜
        total_deaths[square_idx]++;
        if (is_white) {
            white_total_deaths[square_idx]++;
        } else {
            black_total_deaths[square_idx]++;
        }

        // 2. 更新细分榜 (利用 Tracker 的 ID 直接作为 Key)
        detailed_deaths[full_id][square_idx]++;

        // 3. 额外聚合逻辑 (可选)：
        // 如果 ID 是 "White_Pawn_a"，我们也给 "White_Pawn" 增加计数，
        // 这样在 Python 画图时既看得到 a 兵，也能方便地画出所有兵的总图。
        if (full_id.find("Pawn") != std::string::npos) {
            std::string generic_key = is_white ? "White_Pawn_All" : "Black_Pawn_All";
            detailed_deaths[generic_key][square_idx]++;
        }
    }
};

class DetailedDeathVisitor : public chess::pgn::Visitor {
    chess::Board board;
    AdvancedTracker tracker;
    DeathStats& stats;

public:
    DetailedDeathVisitor(DeathStats& s) : stats(s) {
        board.setFen(chess::constants::STARTPOS);
        tracker.initialize_ids();
    }

    void startPgn() override {
        board.setFen(chess::constants::STARTPOS);
        tracker.initialize_ids();
    }

    void header(std::string_view key, std::string_view value) override {}
    void startMoves() override {}

    void move(std::string_view move_str, std::string_view comment) override {
        chess::Move move;
        try {
            move = chess::uci::parseSan(board, move_str);
        } catch (...) { return; }
        process_move_logic(move);
    }

    void endPgn() override {
        stats.games_processed++;
        if (stats.games_processed % 10000 == 0) {
            std::cout << "Processed: " << stats.games_processed << "\r" << std::flush;
        }
    }

private:
    void process_move_logic(chess::Move move) {
        auto from_sq = move.from();
        auto to_sq = move.to();

        // 异常防御
        if (!tracker.board_map[from_sq.index()].active) {
            board.makeMove(move);
            return;
        }

        // 提取攻击者信息 (用于移动 tracker)
        TrackedPiece attacker_data = tracker.board_map[from_sq.index()];
        chess::PieceType p_type = board.at<chess::PieceType>(from_sq);

        // --- 逻辑判断 ---
        bool is_castling = (move.typeOf() == chess::Move::CASTLING);
        if (!is_castling && p_type == chess::PieceType::KING) {
            if (std::abs(to_sq.file() - from_sq.file()) > 1) is_castling = true;
        }

        bool is_en_passant = (move.typeOf() == chess::Move::ENPASSANT);
        if (!is_en_passant && p_type == chess::PieceType::PAWN) {
            if (to_sq.file() != from_sq.file() && board.at(to_sq) == chess::Piece::NONE) {
                is_en_passant = true;
            }
        }

        bool is_promotion = (move.typeOf() == chess::Move::PROMOTION);
        if (!is_promotion && p_type == chess::PieceType::PAWN) {
            chess::Rank end_rank = (board.sideToMove() == chess::Color::WHITE) ? chess::Rank::RANK_8 : chess::Rank::RANK_1;
            if (to_sq.rank() == end_rank) is_promotion = true;
        }

        // ==========================
        // 1. 处理吃子 (Capture)
        // ==========================
        bool physical_capture = board.isCapture(move);
        if (is_en_passant) physical_capture = true;

        if (physical_capture && !is_castling) {
            chess::Square victim_sq = to_sq;
            if (is_en_passant) {
                victim_sq = chess::Square(to_sq.file(), from_sq.rank());
            }

            // 从 Tracker 获取 ID (例如 "White_Pawn_a")
            TrackedPiece victim_data = tracker.board_map[victim_sq.index()];
            
            if (victim_data.active) {
                bool is_white_victim = (victim_data.id.find("White") != std::string::npos);
                stats.commit_death(victim_data.id, victim_sq.index(), is_white_victim);
                
                // 从 Tracker 移除受害者
                tracker.board_map[victim_sq.index()] = {"", false, false};
            }
        }

        // ==========================
        // 2. 更新 Tracker 位置
        // ==========================
        
        // 移除原位置
        tracker.board_map[from_sq.index()] = {"", false, false};

        if (is_promotion) attacker_data.promoted = true;

        if (is_castling) {
            // 处理王车易位的特殊位置更新
            bool kingSide = to_sq > from_sq;
            chess::Color c = board.sideToMove();
            chess::Square actual_king_to = chess::Square::castling_king_square(kingSide, c);
            chess::Square actual_rook_to = chess::Square::castling_rook_square(kingSide, c);
            
            chess::Rank rank = (c == chess::Color::WHITE) ? chess::Rank::RANK_1 : chess::Rank::RANK_8;
            chess::Square rook_from = kingSide ? chess::Square(chess::File::FILE_H, rank) : chess::Square(chess::File::FILE_A, rank);

            // 移动车
            if (tracker.board_map[rook_from.index()].active) {
                TrackedPiece rook_data = tracker.board_map[rook_from.index()];
                tracker.board_map[rook_from.index()] = {"", false, false};
                tracker.board_map[actual_rook_to.index()] = rook_data;
            }
            // 移动王
            tracker.board_map[actual_king_to.index()] = attacker_data;
        } else {
            // 普通移动
            tracker.board_map[to_sq.index()] = attacker_data;
        }

        // ==========================
        // 3. Engine 走棋
        // ==========================
        board.makeMove(move);

        // ==========================
        // 4. 处理 将杀 (Checkmate)
        // ==========================
        if (board.inCheck()) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            
            if (moves.empty()) {
                // 发生将杀！
                // 输家是当前轮到走棋的一方 (board.sideToMove())
                chess::Color loser_color = board.sideToMove();
                
                // 找到输家的王的位置
                chess::Square king_sq = board.kingSq(loser_color);
                
                // 从 Tracker 获取 ID (虽然肯定是 King，但为了保持 ID 格式一致)
                TrackedPiece king_data = tracker.board_map[king_sq.index()];
                
                if (king_data.active) {
                    bool is_white_loser = (loser_color == chess::Color::WHITE);
                    // 记录 King 的“死亡”
                    stats.commit_death(king_data.id, king_sq.index(), is_white_loser);
                }
            }
        }
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (!fs::exists(OUTCOME_DIR)) {
        fs::create_directories(OUTCOME_DIR);
    }

    DeathStats stats;
    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);

    if (!pgn_file.is_open()) {
        std::cerr << "Error: PGN file not found." << std::endl;
        return 1;
    }

    std::cout << "Starting Detailed Analysis (v2)..." << std::endl;

    DetailedDeathVisitor visitor(stats);
    chess::pgn::StreamParser<1024> parser(pgn_file); // 1MB Buffer
    
    parser.readGames(visitor);

    std::cout << "\nAnalysis Complete." << std::endl;
    std::cout << "Total Deaths (incl. Checkmates): " << stats.total_death_count << std::endl;

    // --- 构建 JSON ---
    json j_root;
    
    j_root["meta_info"] = {
        {"games_processed", stats.games_processed},
        {"total_events", stats.total_death_count},
        {"note", "Includes Checkmates as King deaths. Specific pawns tracked."}
    };

    j_root["death_heatmaps"]["summary"]["total"] = stats.total_deaths;
    j_root["death_heatmaps"]["summary"]["white_total"] = stats.white_total_deaths;
    j_root["death_heatmaps"]["summary"]["black_total"] = stats.black_total_deaths;

    for (const auto& [key, arr] : stats.detailed_deaths) {
        j_root["death_heatmaps"]["detailed"][key] = arr;
    }

    std::ofstream out_file(FINAL_RESULT_FILE);
    out_file << std::setw(4) << j_root << std::endl;

    std::cout << "Saved to: " << FINAL_RESULT_FILE << std::endl;

    return 0;
}