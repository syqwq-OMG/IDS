#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <filesystem>
#include <chrono>
#include <iomanip>

// 包含你上传的 chess.hpp 和常用的 nlohmann/json 库
#include "chess.hpp"
#include "json.hpp" 

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "dataset/lichess_db_standard_rated_2025-11.pgn";
const std::string OUTCOME_DIR = "outcome/chess_killer";
const std::string CHECKPOINT_FILE = OUTCOME_DIR + "/checkpoint_cpp.json";
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/final_matrix.json";

// 统计上限回合数 (索引0-74对应1-75回合，索引75对应76+回合)
const int MAX_TURN_INDEX = 75; 
const int SAVE_INTERVAL = 10000;  // 每处理多少局保存一次
// ===========================================

// 跟踪棋子状态的结构体
struct TrackedPiece {
    std::string id;
    bool promoted = false;
    bool active = false; // 相当于 Python 中的 None 检查
};

// 全局统计数据结构
struct GlobalStats {
    // Kill Matrix: AttackerID -> VictimID -> TurnCounts (Array of 76 ints)
    std::map<std::string, std::map<std::string, std::vector<int>>> kill_matrix;
    // Death Timeline: VictimID -> TurnCounts
    std::map<std::string, std::vector<int>> death_timeline;

    void update(const std::string& attacker_id, const std::string& victim_id, int turn_idx) {
        if (turn_idx < 0) turn_idx = 0;
        if (turn_idx > MAX_TURN_INDEX) turn_idx = MAX_TURN_INDEX;

        // 1. 更新 Kill Matrix
        auto& turns = kill_matrix[attacker_id][victim_id];
        if (turns.empty()) turns.resize(MAX_TURN_INDEX + 1, 0);
        turns[turn_idx]++;

        // 2. 更新 Death Timeline
        auto& d_turns = death_timeline[victim_id];
        if (d_turns.empty()) d_turns.resize(MAX_TURN_INDEX + 1, 0);
        d_turns[turn_idx]++;
    }
};

// 高级追踪器：负责维护棋盘上每个格子的“身份”
class AdvancedTracker {
public:
    std::array<TrackedPiece, 64> board_map;

    AdvancedTracker() {
        initialize_ids();
    }

    void initialize_ids() {
        // 清空
        for(auto& p : board_map) p = { "", false, false };

        const std::vector<char> files = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};

        // 辅助 lambda
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

// PGN 访问者：负责解析游戏流
class KillerVisitor : public chess::pgn::Visitor {
public:
    chess::Board board;
    AdvancedTracker tracker;
    GlobalStats& stats;
    
    int games_processed_count = 0;
    int target_skip_count = 0; // 用于断点续传，跳过前 N 局
    
    std::chrono::steady_clock::time_point start_time;

    KillerVisitor(GlobalStats& s, int start_count) : stats(s), games_processed_count(start_count), target_skip_count(start_count) {
        start_time = std::chrono::steady_clock::now();
    }

    void startPgn() override {
        // 如果还需要跳过旧的对局
        if (games_processed_count < target_skip_count) {
            skipPgn(true); // 这是一个 chess.hpp 的功能，只会解析 Header，跳过 Moves
            return;
        }

        // 新对局初始化
        board = chess::Board(); 
        tracker.initialize_ids();
    }

    void header(std::string_view key, std::string_view value) override {
        // 不需要处理 Header
    }

    void startMoves() override {
        // 准备开始处理 Move
    }

    void move(std::string_view move_str, std::string_view comment) override {
        chess::Move move;
        try {
            // 解析 SAN (例如 "Nf3") 到 Move 对象，需要当前 board 上下文
            move = chess::uci::parseSan(board, move_str); 
        } catch (...) {
            // 如果解析失败（通常是数据损坏），跳过此步
            return; 
        }

        process_move_logic(move);
    }

    void endPgn() override {
        games_processed_count++;

        // 定期打印进度和保存 Checkpoint
        if (games_processed_count > target_skip_count && games_processed_count % SAVE_INTERVAL == 0) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start_time;
            double speed = (games_processed_count - target_skip_count) / elapsed.count();
            
            std::cout << "Processed: " << games_processed_count 
                      << " | Time: " << std::fixed << std::setprecision(1) << elapsed.count() << "s"
                      << " | Speed: " << speed << " g/s" << std::endl;

            save_checkpoint();
        }
    }

    // 保存检查点
    void save_checkpoint() {
        json j;
        j["games_processed"] = games_processed_count;
        j["current_pgn_offset"] = 0; // C++版本使用 Skip 计数逻辑，不再依赖文件 offset
        
        j["stats"]["kill_matrix"] = stats.kill_matrix;
        j["stats"]["death_timeline"] = stats.death_timeline;

        std::string temp_path = CHECKPOINT_FILE + ".tmp";
        std::ofstream o(temp_path);
        o << j;
        o.close();
        
        // 原子替换
        try {
            fs::rename(temp_path, CHECKPOINT_FILE);
        } catch (...) {
            std::cerr << "Error saving checkpoint." << std::endl;
        }
    }

private:
    void process_move_logic(chess::Move move) {
        auto from_sq = move.from();
        auto to_sq = move.to();
        
        // 异常防御：如果 tracker 认为这里是空的，但引擎却生成了移动，说明状态可能同步错误，直接执行引擎移动并返回
        if (!tracker.board_map[from_sq.index()].active) {
            board.makeMove(move);
            return; 
        }

        auto& attacker_data = tracker.board_map[from_sq.index()];
        std::string attacker_id = tracker.get_full_id(attacker_data);
        
        // 计算回合数 (0-based)
        // fullMoveNumber 从 1 开始，所以 -1
        int turn_idx = board.fullMoveNumber() - 1; 
        if (turn_idx > MAX_TURN_INDEX) turn_idx = MAX_TURN_INDEX;

        // --- 1. 处理吃子 (Capture) ---
        if (board.isCapture(move)) {
            chess::Square victim_sq = to_sq;
            
            // 特殊情况：吃过路兵 (En Passant)
            // 此时棋子实际上是在 to_sq 的“后面”
            if (move.typeOf() == chess::Move::ENPASSANT) {
                // 吃过路兵时，被吃掉的兵在 to_sq 的同一列，但 rank 取决于 from_sq (也就是吃子兵所在的 rank)
                victim_sq = chess::Square(to_sq.file(), from_sq.rank());
            }

            auto& victim_data = tracker.board_map[victim_sq.index()];
            if (victim_data.active) {
                std::string victim_id = tracker.get_full_id(victim_data);
                stats.update(attacker_id, victim_id, turn_idx);
                // 移除被吃掉的棋子
                victim_data = {"", false, false};
            }
        }

        // --- 2. 移动 Tracker ---
        
        // 处理易位 (Castling) - 需要手动移动车
        if (move.typeOf() == chess::Move::CASTLING) {
            bool kingSide = to_sq > from_sq;
            chess::Color c = board.sideToMove();
            chess::Rank rank = (c == chess::Color::WHITE) ? chess::Rank::RANK_1 : chess::Rank::RANK_8;
            
            chess::Square rook_from, rook_to;
            if (kingSide) { // O-O (短易位)
                rook_from = chess::Square(chess::File::FILE_H, rank);
                rook_to = chess::Square(chess::File::FILE_F, rank);
            } else { // O-O-O (长易位)
                rook_from = chess::Square(chess::File::FILE_A, rank);
                rook_to = chess::Square(chess::File::FILE_D, rank);
            }
            
            // 移动车
            tracker.board_map[rook_to.index()] = tracker.board_map[rook_from.index()];
            tracker.board_map[rook_from.index()] = {"", false, false};
        }

        // 移动攻击者 (主棋子)
        tracker.board_map[to_sq.index()] = attacker_data;
        tracker.board_map[from_sq.index()] = {"", false, false};

        // 处理升变 (Promotion)
        if (move.typeOf() == chess::Move::PROMOTION) {
            tracker.board_map[to_sq.index()].promoted = true;
        }

        // --- 3. 更新引擎棋盘状态 ---
        board.makeMove(move);

        // --- 4. 处理将杀 (Checkmate) ---
        // 检查当前方是否已经无路可走且被将军
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, board);
        
        if (moves.empty() && board.inCheck()) {
            // 发生了将杀
            // 此时 board.sideToMove() 是输家（被将死的一方）
            
            // 攻击者已经在 to_sq (如果刚才那步是升变，这里需要获取最新的 promoted 状态)
            auto& final_attacker_data = tracker.board_map[to_sq.index()];
            std::string final_attacker_id = tracker.get_full_id(final_attacker_data);

            // 受害者是输家的王
            std::string loser_color = (board.sideToMove() == chess::Color::WHITE) ? "White" : "Black";
            std::string victim_id = loser_color + "_King";

            stats.update(final_attacker_id, victim_id, turn_idx);
        }
    }
};

int main() {
    // 确保输出目录存在
    if (!fs::exists(OUTCOME_DIR)) {
        fs::create_directories(OUTCOME_DIR);
    }

    GlobalStats stats;
    int start_games = 0;

    // --- 断点续传逻辑 ---
    if (fs::exists(CHECKPOINT_FILE)) {
        std::cout << "Found checkpoint: " << CHECKPOINT_FILE << std::endl;
        try {
            std::ifstream i(CHECKPOINT_FILE);
            json j;
            i >> j;
            
            start_games = j["games_processed"];
            
            // 恢复统计数据
            if (j.contains("stats")) {
                if (j["stats"].contains("kill_matrix"))
                    stats.kill_matrix = j["stats"]["kill_matrix"].get<std::map<std::string, std::map<std::string, std::vector<int>>>>();
                if (j["stats"].contains("death_timeline"))
                    stats.death_timeline = j["stats"]["death_timeline"].get<std::map<std::string, std::vector<int>>>();
            }
            std::cout << "Resuming from Game " << start_games << " (Skipping previous games...)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Checkpoint load failed: " << e.what() << ". Starting fresh." << std::endl;
            start_games = 0;
        }
    }

    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "Error: PGN file not found at " << PGN_FILE_PATH << std::endl;
        return 1;
    }

    std::cout << "Starting Analysis..." << std::endl;

    // 初始化访问者
    KillerVisitor visitor(stats, start_games);
    
    // 初始化解析器
    chess::pgn::StreamParser parser(pgn_file);
    
    // 开始读取 (会自动调用 visitor 的回调)
    parser.readGames(visitor);

    // --- 最终保存 ---
    json j;
    j["stats"]["kill_matrix"] = stats.kill_matrix;
    j["stats"]["death_timeline"] = stats.death_timeline;
    
    std::cout << "Saving final results to " << FINAL_RESULT_FILE << "..." << std::endl;
    std::ofstream o(FINAL_RESULT_FILE);
    o << j.dump(4); // 缩进4空格，与Python一致
    o.close();

    std::cout << "Done!" << std::endl;

    return 0;
}