#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <sstream> // <--- [新增] 引入 stringstream

#include "../chess.hpp"
#include "../json.hpp" 
using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
// const std::string PGN_FILE_PATH = "dataset/lichess_db_standard_rated_2025-11.pgn";
const std::string PGN_FILE_PATH = "../dataset/catcatX.pgn";
const std::string OUTCOME_DIR = "../outcome/chess_killer";
const std::string CHECKPOINT_FILE = OUTCOME_DIR + "/checkpoint_cpp_test.json";
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/final_matrix_cpp.json";

const int MAX_TURN_INDEX = 75; 
const int SAVE_INTERVAL = 10000;
// ===========================================

// 追踪棋子的数据结构
struct TrackedPiece {
    std::string id;
    bool promoted = false;
    bool active = false; // true 表示该位置有棋子，false 表示 None
};

// 待提交的击杀事件
struct PendingEvent {
    std::string attacker_id;
    std::string victim_id;
    int turn_idx;
};

// 全局统计 (使用 unordered_map 提升运行时性能)
// 结构: AttackerID -> VictimID -> TurnCounts
using KillMatrix = std::unordered_map<std::string, std::unordered_map<std::string, std::vector<int>>>;
using DeathTimeline = std::unordered_map<std::string, std::vector<int>>;

struct GlobalStats {
    KillMatrix kill_matrix;
    DeathTimeline death_timeline;

    void commit_event(const PendingEvent& e) {
        // 1. 更新 Kill Matrix
        auto& victim_map = kill_matrix[e.attacker_id];
        auto& turns = victim_map[e.victim_id];
        if (turns.empty()) turns.resize(MAX_TURN_INDEX + 1, 0);
        
        // 边界检查
        int safe_idx = std::min(e.turn_idx, MAX_TURN_INDEX);
        turns[safe_idx]++;

        // 2. 更新 Death Timeline
        auto& d_turns = death_timeline[e.victim_id];
        if (d_turns.empty()) d_turns.resize(MAX_TURN_INDEX + 1, 0);
        d_turns[safe_idx]++;
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

    // --- [新增] PGN 录制缓存 ---
    std::stringstream current_pgn_ss; 

    int games_processed_count = 0;
    int target_skip_count = 0;
    std::chrono::steady_clock::time_point start_time;

    KillerVisitor(GlobalStats& s, int start_count) 
        : stats(s), games_processed_count(start_count), target_skip_count(start_count) {
        start_time = std::chrono::steady_clock::now();
        current_game_events.reserve(150);
    }

    void startPgn() override {
        // --- [新增] 重置 PGN 缓存 ---
        current_pgn_ss.str(""); 
        current_pgn_ss.clear();

        // 断点续传逻辑
        if (games_processed_count < target_skip_count) {
            skipPgn(true); 
            return;
        }

        board = chess::Board(); 
        tracker.initialize_ids();
        current_game_events.clear();
        current_game_invalid = false;
    }

    void header(std::string_view key, std::string_view value) override {
        // --- [新增] 录制 Header ---
        // 只有当我们正在处理（没被跳过）时才录制，节省内存/时间
        if (games_processed_count >= target_skip_count) {
            current_pgn_ss << "[" << key << " \"" << value << "\"]\n";
        }
    }

    void startMoves() override {
        if (games_processed_count >= target_skip_count) {
            current_pgn_ss << "\n"; // Header 和 Moves 之间空一行
        }
    }

    void move(std::string_view move_str, std::string_view comment) override {
        if (current_game_invalid) return;

        // --- [新增] 录制 Move ---
        if (games_processed_count >= target_skip_count) {
            // 简单的格式化：如果是白方，加上回合数 "1. "
            if (board.sideToMove() == chess::Color::WHITE) {
                current_pgn_ss << board.fullMoveNumber() << ". " << move_str << " ";
            } else {
                current_pgn_ss << move_str << " ";
            }
        }

        chess::Move move;
        try {
            // parseSan 可能会抛出异常
            move = chess::uci::parseSan(board, move_str); 
        } catch (const std::exception& e) {
            current_game_invalid = true;
            
            // --- [新增] 触发 Debug 输出 ---
            print_debug_info(move_str, e.what());
            
            return; 
        }
        
        // 额外的逻辑安全检查：如果 parseSan 通过了，但内部逻辑崩了
        // 比如 process_move_logic 里如果发现异常也可以手动设置 invalid
        try {
             process_move_logic(move);
        } catch (const std::exception& e) {
             current_game_invalid = true;
             print_debug_info(move_str, std::string("Logic Error: ") + e.what());
        }
    }

    // --- [新增] 封装打印逻辑 ---
    void print_debug_info(std::string_view current_move, std::string_view error_msg) {
        // 使用 cerr 输出错误，以免污染标准输出的数据流
        std::cerr << "\n========================================\n";
        std::cerr << "[DEBUG] Invalid Game Detected!\n";
        std::cerr << "Game Index: " << games_processed_count << "\n";
        std::cerr << "Error: " << error_msg << "\n";
        std::cerr << "Problematic Move: " << current_move << "\n";
        std::cerr << "Board FEN: " << board.getFen() << "\n";
        std::cerr << "---------------- PGN DUMP ----------------\n";
        std::cerr << current_pgn_ss.str() << "\n";
        std::cerr << "========================================\n" << std::endl;
    }

    void endPgn() override {
        // ... (保持原有逻辑不变)
        if (games_processed_count < target_skip_count) {
            games_processed_count++;
            if (games_processed_count % 10000 == 0) {
                 std::cout << "Skipping... " << games_processed_count << " / " << target_skip_count << std::endl;
            }
            return;
        }

        if (!current_game_invalid) {
            for (const auto& e : current_game_events) {
                stats.commit_event(e);
            }
        } else {
            // 可选：如果不希望刷屏，可以在这里打印 "Game X skipped due to error"
        }
        
        games_processed_count++;

        if (games_processed_count % SAVE_INTERVAL == 0) {
            print_progress();
            save_checkpoint();
        }
    }

    // ... (print_progress, save_checkpoint 保持不变) ...
    void print_progress() {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - start_time;
        // 计算实际处理速度（排除掉跳过的部分）
        double processed_new = games_processed_count - target_skip_count;
        double speed = (elapsed.count() > 0) ? processed_new / elapsed.count() : 0.0;
        
        std::cout << "Processed: " << games_processed_count 
                  << " | Time: " << std::fixed << std::setprecision(1) << elapsed.count() << "s"
                  << " | Speed: " << speed << " g/s" << std::endl;
    }

    void save_checkpoint() {
        // 转换为 ordered map 以便保存出的 json 有序（可选，主要为了美观）
        std::map<std::string, std::map<std::string, std::vector<int>>> ordered_matrix;
        for (const auto& [atk, victims] : stats.kill_matrix) {
            for (const auto& [vic, turns] : victims) {
                ordered_matrix[atk][vic] = turns;
            }
        }
        std::map<std::string, std::vector<int>> ordered_timeline;
        for (const auto& [vic, turns] : stats.death_timeline) {
            ordered_timeline[vic] = turns;
        }

        json j;
        j["games_processed"] = games_processed_count;
        j["stats"]["kill_matrix"] = ordered_matrix;
        j["stats"]["death_timeline"] = ordered_timeline;

        std::string temp_path = CHECKPOINT_FILE + ".tmp";
        std::ofstream o(temp_path);
        o << j; // 不使用 indent 以节省磁盘IO时间
        o.close();
        try { fs::rename(temp_path, CHECKPOINT_FILE); } catch (...) {}
    }

private:
    void process_move_logic(chess::Move move) {
        // ... (保持原有逻辑不变) ...
        auto from_sq = move.from();
        auto to_sq = move.to();
        
        // 异常防御
        if (!tracker.board_map[from_sq.index()].active) {
            // 这里虽然目前代码是 return，但如果您认为这属于数据不一致的严重错误
            // 也可以在这里设置 current_game_invalid = true 并 throw 异常来触发打印
            // 目前保持原样，只做合法移动
            board.makeMove(move);
            return; 
        }

        // ... (剩余逻辑保持不变) ...
        // 复制攻击者数据 (Value Copy)
        TrackedPiece attacker_data = tracker.board_map[from_sq.index()];
        std::string attacker_id = tracker.get_full_id(attacker_data);
        
        // 计算回合数 (FullMoveNumber)
        int turn_idx = board.fullMoveNumber() - 1; 
        if (turn_idx < 0) turn_idx = 0;
        
        // ====================================================================
        // 逻辑修正核心：手动判定棋步类型，不完全依赖 parseSan 返回的 flag
        // ====================================================================
        
        chess::PieceType p_type = board.at<chess::PieceType>(from_sq);
        
        // 1. 判定是否为易位 (King 移动超过1格) [ cite: chess.hpp ]
        bool is_castling = (move.typeOf() == chess::Move::CASTLING);
        if (!is_castling && p_type == chess::PieceType::KING) {
            if (std::abs(to_sq.file() - from_sq.file()) > 1) {
                is_castling = true;
            }
        }

        // 2. 判定是否为吃过路兵 (Pawn 斜走且目标格为空)
        bool is_en_passant = (move.typeOf() == chess::Move::ENPASSANT);
        if (!is_en_passant && p_type == chess::PieceType::PAWN) {
            if (to_sq.file() != from_sq.file() && board.at(to_sq) == chess::Piece::NONE) {
                // 且这步是合法的斜走，必定是EP
                is_en_passant = true;
            }
        }

        // 3. 判定是否升变
        bool is_promotion = (move.typeOf() == chess::Move::PROMOTION);
        // 如果库没有设置flag，我们需要根据规则补充判断：兵走到底线
        if (!is_promotion && p_type == chess::PieceType::PAWN) {
            chess::Rank end_rank = (board.sideToMove() == chess::Color::WHITE) ? chess::Rank::RANK_8 : chess::Rank::RANK_1;
            if (to_sq.rank() == end_rank) {
                is_promotion = true;
            }
        }

        // --- Step 1: 处理 Capture (击杀) ---
        bool physical_capture = board.isCapture(move);
        // 如果判定为 EP，即使 board.isCapture 返回 false (某些库对EP定义不同)，我们也视为吃子
        if (is_en_passant) physical_capture = true;

        if (physical_capture) {
            chess::Square victim_sq = to_sq;
            
            // 吃过路兵修正受害者位置
            if (is_en_passant) {
                victim_sq = chess::Square(to_sq.file(), from_sq.rank());
            }

            TrackedPiece victim_data = tracker.board_map[victim_sq.index()];
            if (victim_data.active) {
                std::string victim_id = tracker.get_full_id(victim_data);
                current_game_events.push_back({attacker_id, victim_id, turn_idx});
                
                // 移除受害者
                tracker.board_map[victim_sq.index()] = {"", false, false};
            }
        }

        // --- Step 2: 处理 Tracker 移动 ---
        
        // 1. 确定王最终落子的位置 (Fix Bug Here)
        int final_king_sq_idx = to_sq.index();

        if (is_castling) {
            bool kingSide = to_sq > from_sq; // 在该库中，H1 > E1 成立，逻辑通用
            chess::Color c = board.sideToMove();
            
            // 使用库提供的辅助函数计算王和车的实际落点
            chess::Square actual_king_to = chess::Square::castling_king_square(kingSide, c);
            chess::Square actual_rook_to = chess::Square::castling_rook_square(kingSide, c);
            
            // 获取车的来源位置（在该库中，move.to() 实际上指向车的来源位置，即 H1/A1）
            // 但为了保险，还是重新计算标准车位
            chess::Rank rank = (c == chess::Color::WHITE) ? chess::Rank::RANK_1 : chess::Rank::RANK_8;
            chess::Square rook_from = kingSide ? chess::Square(chess::File::FILE_H, rank) : chess::Square(chess::File::FILE_A, rank);

            // 更新车的位置
            if (tracker.board_map[rook_from.index()].active) {
                TrackedPiece rook_data = tracker.board_map[rook_from.index()];
                tracker.board_map[rook_from.index()] = {"", false, false};
                tracker.board_map[actual_rook_to.index()] = rook_data;
            }

            // 修正王的落点索引
            final_king_sq_idx = actual_king_to.index();
        }

        // 2. 移动主棋子 (Attacker / King)
        tracker.board_map[from_sq.index()] = {"", false, false};
        
        // 处理升变标记
        if (is_promotion) {
            attacker_data.promoted = true;
        }
        
        // 将 Attacker 放入修正后的位置
        tracker.board_map[final_king_sq_idx] = attacker_data;

        // --- Step 3: 更新 Engine Board ---
        board.makeMove(move);

        // --- Step 4: 处理 Checkmate (将杀) ---
        if (board.inCheck()) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);
            
            if (moves.empty()) {
                // 注意：这里也要用修正后的位置找凶手
                auto& final_attacker_data = tracker.board_map[final_king_sq_idx];
                
                std::string final_attacker_id = tracker.get_full_id(final_attacker_data);
                if (final_attacker_id.empty()) final_attacker_id = "Unknown_Ghost";

                std::string loser_color = (board.sideToMove() == chess::Color::WHITE) ? "White" : "Black";
                std::string victim_id = loser_color + "_King";

                current_game_events.push_back({final_attacker_id, victim_id, turn_idx});
            }
        }
    }
};

// ... (main 函数保持不变) ...


int main() {
    std::cout<<111111111<<std::endl;
    // 提升IO性能
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (!fs::exists(OUTCOME_DIR)) fs::create_directories(OUTCOME_DIR);

    GlobalStats stats;
    int start_games = 0;

    // --- 加载 Checkpoint ---
    if (fs::exists(CHECKPOINT_FILE)) {
        std::cout << "Loading checkpoint: " << CHECKPOINT_FILE << std::endl;
        try {
            std::ifstream i(CHECKPOINT_FILE);
            json j;
            i >> j;
            start_games = j["games_processed"];
            
            // 恢复统计数据
            if (j.contains("stats")) {
                if (j["stats"].contains("kill_matrix")) {
                    auto j_matrix = j["stats"]["kill_matrix"];
                    for (auto it = j_matrix.begin(); it != j_matrix.end(); ++it) {
                        std::string atk = it.key();
                        for (auto it2 = it.value().begin(); it2 != it.value().end(); ++it2) {
                            stats.kill_matrix[atk][it2.key()] = it2.value().get<std::vector<int>>();
                        }
                    }
                }
                if (j["stats"].contains("death_timeline")) {
                    auto j_timeline = j["stats"]["death_timeline"];
                    for (auto it = j_timeline.begin(); it != j_timeline.end(); ++it) {
                        stats.death_timeline[it.key()] = it.value().get<std::vector<int>>();
                    }
                }
            }
            std::cout << "Resuming from Game Count: " << start_games << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Checkpoint load error: " << e.what() << ". Starting fresh." << std::endl;
            start_games = 0;
            stats = GlobalStats();
        }
    }

    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "PGN file not found: " << PGN_FILE_PATH << std::endl;
        return 1;
    }

    // 初始化访问器
    KillerVisitor visitor(stats, start_games);
    // 使用 1MB buffer 提升读取速度
    chess::pgn::StreamParser<1024> parser(pgn_file);
    
    std::cout << "Starting Analysis..." << std::endl;
    parser.readGames(visitor);

    // --- 最终保存 ---
    visitor.save_checkpoint();

    // 保存最终人类可读格式 (Indent=4)
    std::cout << "Saving final formatted results..." << std::endl;
    
    std::map<std::string, std::map<std::string, std::vector<int>>> ordered_matrix;
    for (const auto& [atk, victims] : stats.kill_matrix) {
        for (const auto& [vic, turns] : victims) {
            ordered_matrix[atk][vic] = turns;
        }
    }
    std::map<std::string, std::vector<int>> ordered_timeline;
    for (const auto& [vic, turns] : stats.death_timeline) {
        ordered_timeline[vic] = turns;
    }

    json j_final;
    j_final["kill_matrix"] = ordered_matrix;
    j_final["death_timeline"] = ordered_timeline;
    
    std::ofstream o(FINAL_RESULT_FILE);
    o << j_final.dump(4);
    o.close();

    std::cout << "Done! Saved to " << FINAL_RESULT_FILE << std::endl;
    return 0;
}