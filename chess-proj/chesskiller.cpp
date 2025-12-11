#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <filesystem>
#include <chrono>
#include <iomanip>

#include "chess.hpp"
#include "json.hpp" 

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "dataset/lichess_db_standard_rated_2025-11.pgn";
const std::string OUTCOME_DIR = "outcome/chess_killer";
const std::string CHECKPOINT_FILE = OUTCOME_DIR + "/checkpoint_cpp.json";
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/final_matrix.json";

const int MAX_TURN_INDEX = 75; 
const int SAVE_INTERVAL = 10000;
// ===========================================

struct TrackedPiece {
    std::string id;
    bool promoted = false;
    bool active = false; // 相当于 Python 中的 None 检查
};

// 用于暂存一局游戏中的击杀事件 (事务机制)
struct PendingEvent {
    std::string attacker_id;
    std::string victim_id;
    int turn_idx;
};

// 全局统计数据
struct GlobalStats {
    // Kill Matrix: AttackerID -> VictimID -> TurnCounts
    std::map<std::string, std::map<std::string, std::vector<int>>> kill_matrix;
    // Death Timeline: VictimID -> TurnCounts
    std::map<std::string, std::vector<int>> death_timeline;

    void commit_event(const PendingEvent& e) {
        // 1. 更新 Kill Matrix
        auto& turns = kill_matrix[e.attacker_id][e.victim_id];
        if (turns.empty()) turns.resize(MAX_TURN_INDEX + 1, 0);
        turns[e.turn_idx]++;

        // 2. 更新 Death Timeline
        auto& d_turns = death_timeline[e.victim_id];
        if (d_turns.empty()) d_turns.resize(MAX_TURN_INDEX + 1, 0);
        d_turns[e.turn_idx]++;
    }
};

class AdvancedTracker {
public:
    std::array<TrackedPiece, 64> board_map;

    AdvancedTracker() { initialize_ids(); }

    void initialize_ids() {
        // 清空棋盘
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

    std::string get_full_id(const TrackedPiece& p) {
        if (!p.active) return "";
        return p.id + (p.promoted ? "_promoted" : "");
    }
};

class KillerVisitor : public chess::pgn::Visitor {
public:
    chess::Board board;
    AdvancedTracker tracker;
    GlobalStats& stats;
    
    std::vector<PendingEvent> current_game_events;
    bool current_game_invalid = false;

    int games_processed_count = 0;
    int target_skip_count = 0;
    std::chrono::steady_clock::time_point start_time;

    KillerVisitor(GlobalStats& s, int start_count) : stats(s), games_processed_count(start_count), target_skip_count(start_count) {
        start_time = std::chrono::steady_clock::now();
        current_game_events.reserve(100);
    }

    void startPgn() override {
        if (games_processed_count < target_skip_count) {
            skipPgn(true);
            return;
        }
        board = chess::Board(); 
        tracker.initialize_ids();
        current_game_events.clear();
        current_game_invalid = false;
    }

    void header(std::string_view key, std::string_view value) override {}
    void startMoves() override {}

    void move(std::string_view move_str, std::string_view comment) override {
        if (current_game_invalid) return;

        chess::Move move;
        try {
            move = chess::uci::parseSan(board, move_str); 
        } catch (...) {
            current_game_invalid = true;
            skipPgn(true);
            return; 
        }
        process_move_logic(move);
    }

    void endPgn() override {
        games_processed_count++;

        if (!current_game_invalid && games_processed_count > target_skip_count) {
            for (const auto& e : current_game_events) {
                stats.commit_event(e);
            }
        }

        if (games_processed_count > target_skip_count && games_processed_count % SAVE_INTERVAL == 0) {
            print_progress();
            save_checkpoint();
        }
    }

    void print_progress() {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        double speed = (games_processed_count - target_skip_count) / elapsed.count();
        std::cout << "Processed: " << games_processed_count 
                  << " | Time: " << std::fixed << std::setprecision(1) << elapsed.count() << "s"
                  << " | Speed: " << speed << " g/s" << std::endl;
    }

    void save_checkpoint() {
        json j;
        j["games_processed"] = games_processed_count;
        j["current_pgn_offset"] = 0;
        
        // Checkpoint 结构保持嵌套
        j["stats"]["kill_matrix"] = stats.kill_matrix;
        j["stats"]["death_timeline"] = stats.death_timeline;

        std::string temp_path = CHECKPOINT_FILE + ".tmp";
        std::ofstream o(temp_path);
        o << j;
        o.close();
        try { fs::rename(temp_path, CHECKPOINT_FILE); } catch (...) {}
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

        // =========================================================
        // 关键修正：这里必须使用值拷贝 (TrackedPiece)，不能用 auto&
        // 否则后续清空 from_sq 时，attacker_data 也会变为空！
        // =========================================================
        TrackedPiece attacker_data = tracker.board_map[from_sq.index()];
        std::string attacker_id = tracker.get_full_id(attacker_data);
        
        int turn_idx = board.fullMoveNumber() - 1; 
        if (turn_idx < 0) turn_idx = 0;
        if (turn_idx > MAX_TURN_INDEX) turn_idx = MAX_TURN_INDEX;

        // 1. Capture Logic
        if (board.isCapture(move)) {
            chess::Square victim_sq = to_sq;
            if (move.typeOf() == chess::Move::ENPASSANT) {
                victim_sq = chess::Square(to_sq.file(), from_sq.rank());
            }

            // 获取受害者数据 (值拷贝)
            TrackedPiece victim_data = tracker.board_map[victim_sq.index()];
            
            if (victim_data.active) {
                std::string victim_id = tracker.get_full_id(victim_data);
                current_game_events.push_back({attacker_id, victim_id, turn_idx});
                
                // 移除受害者
                tracker.board_map[victim_sq.index()] = {"", false, false};
            }
        }

        // 2. Move Tracker Logic
        // 处理易位 (Move Rook)
        if (move.typeOf() == chess::Move::CASTLING) {
            bool kingSide = to_sq > from_sq;
            chess::Color c = board.sideToMove();
            chess::Rank rank = (c == chess::Color::WHITE) ? chess::Rank::RANK_1 : chess::Rank::RANK_8;
            
            chess::Square rook_from = kingSide ? chess::Square(chess::File::FILE_H, rank) : chess::Square(chess::File::FILE_A, rank);
            chess::Square rook_to = kingSide ? chess::Square(chess::File::FILE_F, rank) : chess::Square(chess::File::FILE_D, rank);
            
            // 移动车 (先拷贝，再清除，再赋值)
            TrackedPiece rook_data = tracker.board_map[rook_from.index()];
            tracker.board_map[rook_from.index()] = {"", false, false};
            tracker.board_map[rook_to.index()] = rook_data;
        }

        // 移动主棋子 (Attacker)
        // 1. 清空原位置
        tracker.board_map[from_sq.index()] = {"", false, false};
        
        // 2. 处理升变标记
        if (move.typeOf() == chess::Move::PROMOTION) {
            attacker_data.promoted = true;
        }
        
        // 3. 放入新位置
        tracker.board_map[to_sq.index()] = attacker_data;

        // 3. Update Engine Board
        board.makeMove(move);

        // 4. Checkmate Logic
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        
        if (moves.empty() && board.inCheck()) {
            // 获取此时位于 to_sq 的攻击者 (也就是刚才移动的棋子)
            // 这里使用引用是可以的，因为移动已经完成，位置已经确定
            auto& final_attacker_data = tracker.board_map[to_sq.index()];
            std::string final_attacker_id = tracker.get_full_id(final_attacker_data);
            
            std::string loser_color = (board.sideToMove() == chess::Color::WHITE) ? "White" : "Black";
            std::string victim_id = loser_color + "_King";

            current_game_events.push_back({final_attacker_id, victim_id, turn_idx});
        }
    }
};

int main() {
    if (!fs::exists(OUTCOME_DIR)) fs::create_directories(OUTCOME_DIR);

    GlobalStats stats;
    int start_games = 0;

    if (fs::exists(CHECKPOINT_FILE)) {
        std::cout << "Loading checkpoint..." << std::endl;
        try {
            std::ifstream i(CHECKPOINT_FILE);
            json j;
            i >> j;
            start_games = j["games_processed"];
            if (j.contains("stats")) {
                if (j["stats"].contains("kill_matrix"))
                    stats.kill_matrix = j["stats"]["kill_matrix"].get<std::map<std::string, std::map<std::string, std::vector<int>>>>();
                if (j["stats"].contains("death_timeline"))
                    stats.death_timeline = j["stats"]["death_timeline"].get<std::map<std::string, std::vector<int>>>();
            }
            std::cout << "Resuming from Game " << start_games << std::endl;
        } catch (...) {
            start_games = 0;
        }
    }

    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "PGN file not found." << std::endl;
        return 1;
    }

    KillerVisitor visitor(stats, start_games);
    chess::pgn::StreamParser parser(pgn_file);
    parser.readGames(visitor);

    // --- 最终保存 ---
    // 确保这里是扁平结构，直接输出 kill_matrix 和 death_timeline
    json j_final;
    j_final["kill_matrix"] = stats.kill_matrix;
    j_final["death_timeline"] = stats.death_timeline;
    
    std::cout << "Saving final results..." << std::endl;
    std::ofstream o(FINAL_RESULT_FILE);
    o << j_final.dump(4);
    o.close();

    std::cout << "Done!" << std::endl;
    return 0;
}