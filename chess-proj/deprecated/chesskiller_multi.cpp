#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "chess.hpp"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH =
    "dataset/lichess_db_standard_rated_2025-11.pgn";
const std::string OUTCOME_DIR = "outcome/chess_killer";
const std::string CHECKPOINT_FILE = OUTCOME_DIR + "/checkpoint_multi.json";
const std::string FINAL_RESULT_FILE =
    OUTCOME_DIR + "/final_matrix_cpp_multi.json";

const int MAX_TURN_INDEX = 150;
const int SAVE_INTERVAL = 20000;    // 每处理多少局保存一次
const size_t MAX_QUEUE_SIZE = 5000; // 任务队列最大长度，防止内存爆炸
const int NUM_THREADS = std::thread::hardware_concurrency() == 0
                            ? 4
                            : std::thread::hardware_concurrency();
// ===========================================

// 追踪棋子的数据结构
struct TrackedPiece {
    std::string id;
    bool promoted = false;
    bool active = false;
};

struct GameTask {
    std::vector<std::string> moves; // 仅存储招法字符串
    bool valid = true;
};

// 线程安全的阻塞队列
template <typename T> class ThreadSafeQueue {
  private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_prod_;
    std::condition_variable cond_cons_;
    size_t max_size_;
    bool stopped_ = false;

  public:
    ThreadSafeQueue(size_t max_size) : max_size_(max_size) {}

    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_prod_.wait(
            lock, [this]() { return queue_.size() < max_size_ || stopped_; });
        if (stopped_)
            return;
        queue_.push(std::move(item));
        cond_cons_.notify_one();
    }

    bool pop(T &item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_cons_.wait(lock, [this]() { return !queue_.empty() || stopped_; });
        if (queue_.empty() && stopped_)
            return false;
        item = std::move(queue_.front());
        queue_.pop();
        cond_prod_.notify_one();
        return true;
    }

    void stop() {
        std::unique_lock<std::mutex> lock(mutex_);
        stopped_ = true;
        cond_cons_.notify_all();
        cond_prod_.notify_all();
    }

    size_t size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// 全局统计结构
using KillMatrix =
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::vector<int>>>;
using DeathTimeline = std::unordered_map<std::string, std::vector<int>>;

struct GlobalStats {
    KillMatrix kill_matrix;
    DeathTimeline death_timeline;

    // 合并另一个 Stats 到当前 Stats (用于汇总线程结果)
    void merge_from(const GlobalStats &other) {
        for (const auto &[atk, victims] : other.kill_matrix) {
            auto &my_victims = kill_matrix[atk];
            for (const auto &[vic, counts] : victims) {
                auto &my_counts = my_victims[vic];
                if (my_counts.empty())
                    my_counts.resize(MAX_TURN_INDEX + 1, 0);

                size_t len = std::min(my_counts.size(), counts.size());
                for (size_t i = 0; i < len; ++i) {
                    my_counts[i] += counts[i];
                }
            }
        }

        for (const auto &[vic, counts] : other.death_timeline) {
            auto &my_counts = death_timeline[vic];
            if (my_counts.empty())
                my_counts.resize(MAX_TURN_INDEX + 1, 0);
            size_t len = std::min(my_counts.size(), counts.size());
            for (size_t i = 0; i < len; ++i) {
                my_counts[i] += counts[i];
            }
        }
    }
};

class AdvancedTracker {
  public:
    std::array<TrackedPiece, 64> board_map;

    AdvancedTracker() { initialize_ids(); }

    void initialize_ids() {
        for (auto &p : board_map)
            p = {"", false, false};
        const std::vector<char> files = {'a', 'b', 'c', 'd',
                                         'e', 'f', 'g', 'h'};
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
        for (int i = 0; i < 8; ++i)
            set(chess::Square(chess::File(i), chess::Rank::RANK_2),
                "White_Pawn_" + std::string(1, files[i]));

        // Black Init
        set(chess::Square::SQ_A8, "Black_Rook_a");
        set(chess::Square::SQ_B8, "Black_Knight_b");
        set(chess::Square::SQ_C8, "Black_Bishop_c");
        set(chess::Square::SQ_D8, "Black_Queen");
        set(chess::Square::SQ_E8, "Black_King");
        set(chess::Square::SQ_F8, "Black_Bishop_f");
        set(chess::Square::SQ_G8, "Black_Knight_g");
        set(chess::Square::SQ_H8, "Black_Rook_h");
        for (int i = 0; i < 8; ++i)
            set(chess::Square(chess::File(i), chess::Rank::RANK_7),
                "Black_Pawn_" + std::string(1, files[i]));
    }

    std::string get_full_id(const TrackedPiece &p) {
        if (!p.active)
            return "";
        return p.id + (p.promoted ? "_promoted" : "");
    }
};

// ================= Worker Logic =================
class GameProcessor {
    chess::Board board;
    AdvancedTracker tracker;
    GlobalStats local_stats;

  public:
    GlobalStats &get_stats() { return local_stats; }

    void process_game(const GameTask &task) {
        board = chess::Board();
        tracker.initialize_ids();

        for (const auto &move_str : task.moves) {
            chess::Move move;
            try {
                // 这是最耗时的操作，现在在Worker线程中并行执行
                move = chess::uci::parseSan(board, move_str);
            } catch (...) {
                break; // 遇到非法招法停止处理该局
            }
            process_move_logic(move);
        }
    }

  private:
    void commit_event(const std::string &attacker, const std::string &victim,
                      int turn) {
        int safe_idx = std::min(turn, MAX_TURN_INDEX);

        // Update Matrix
        auto &victim_map = local_stats.kill_matrix[attacker];
        auto &turns = victim_map[victim];
        if (turns.empty())
            turns.resize(MAX_TURN_INDEX + 1, 0);
        turns[safe_idx]++;

        // Update Timeline
        auto &d_turns = local_stats.death_timeline[victim];
        if (d_turns.empty())
            d_turns.resize(MAX_TURN_INDEX + 1, 0);
        d_turns[safe_idx]++;
    }

    void process_move_logic(chess::Move move) {
        auto from_sq = move.from();
        auto to_sq = move.to();

        if (!tracker.board_map[from_sq.index()].active) {
            board.makeMove(move);
            return;
        }

        TrackedPiece attacker_data = tracker.board_map[from_sq.index()];
        std::string attacker_id = tracker.get_full_id(attacker_data);
        int turn_idx = board.fullMoveNumber() - 1;
        if (turn_idx < 0)
            turn_idx = 0;

        chess::PieceType p_type = board.at<chess::PieceType>(from_sq);

        // Logic Flags
        bool is_castling = (move.typeOf() == chess::Move::CASTLING);
        if (!is_castling && p_type == chess::PieceType::KING &&
            std::abs(to_sq.file() - from_sq.file()) > 1)
            is_castling = true;

        bool is_en_passant = (move.typeOf() == chess::Move::ENPASSANT);
        if (!is_en_passant && p_type == chess::PieceType::PAWN &&
            to_sq.file() != from_sq.file() &&
            board.at(to_sq) == chess::Piece::NONE)
            is_en_passant = true;

        bool is_promotion = (move.typeOf() == chess::Move::PROMOTION);
        if (!is_promotion && p_type == chess::PieceType::PAWN) {
            chess::Rank end_rank = (board.sideToMove() == chess::Color::WHITE)
                                       ? chess::Rank::RANK_8
                                       : chess::Rank::RANK_1;
            if (to_sq.rank() == end_rank)
                is_promotion = true;
        }

        // --- Capture Logic ---
        bool physical_capture = board.isCapture(move);
        if (is_en_passant)
            physical_capture = true;

        if (physical_capture) {
            chess::Square victim_sq = to_sq;
            if (is_en_passant)
                victim_sq = chess::Square(to_sq.file(), from_sq.rank());

            TrackedPiece victim_data = tracker.board_map[victim_sq.index()];
            if (victim_data.active) {
                commit_event(attacker_id, tracker.get_full_id(victim_data),
                             turn_idx);
                tracker.board_map[victim_sq.index()] = {"", false, false};
            }
        }

        // --- Move Logic ---
        int final_king_sq_idx = to_sq.index();

        if (is_castling) {
            bool kingSide = to_sq > from_sq;
            chess::Color c = board.sideToMove();
            chess::Square actual_king_to =
                chess::Square::castling_king_square(kingSide, c);
            chess::Square actual_rook_to =
                chess::Square::castling_rook_square(kingSide, c);

            chess::Rank rank = (c == chess::Color::WHITE) ? chess::Rank::RANK_1
                                                          : chess::Rank::RANK_8;
            chess::Square rook_from =
                kingSide ? chess::Square(chess::File::FILE_H, rank)
                         : chess::Square(chess::File::FILE_A, rank);

            if (tracker.board_map[rook_from.index()].active) {
                TrackedPiece rook_data = tracker.board_map[rook_from.index()];
                tracker.board_map[rook_from.index()] = {"", false, false};
                tracker.board_map[actual_rook_to.index()] = rook_data;
            }
            final_king_sq_idx = actual_king_to.index();
        }

        tracker.board_map[from_sq.index()] = {"", false, false};
        if (is_promotion)
            attacker_data.promoted = true;
        tracker.board_map[final_king_sq_idx] = attacker_data;

        board.makeMove(move);

        // --- Checkmate Logic ---
        if (board.inCheck()) {
            chess::Movelist moves;
            chess::movegen::legalmoves(moves, board);

            if (moves.empty()) {
                auto &final_attacker_data =
                    tracker.board_map[final_king_sq_idx];
                std::string final_attacker_id =
                    tracker.get_full_id(final_attacker_data);
                if (final_attacker_id.empty())
                    final_attacker_id = "Unknown_Ghost";

                std::string loser_color =
                    (board.sideToMove() == chess::Color::WHITE) ? "White"
                                                                : "Black";
                commit_event(final_attacker_id, loser_color + "_King",
                             turn_idx);
            }
        }
    }
};

// ================= Dispatcher Visitor =================
// 仅负责收集Move字符串，不做逻辑计算
class DispatcherVisitor : public chess::pgn::Visitor {
  public:
    ThreadSafeQueue<GameTask> &queue;
    std::vector<std::string> current_moves;
    std::atomic<int> &read_counter;
    int skip_target = 0;
    int current_index = 0;

    DispatcherVisitor(ThreadSafeQueue<GameTask> &q, std::atomic<int> &cnt,
                      int start_idx)
        : queue(q), read_counter(cnt), skip_target(start_idx),
          current_index(0) {
        current_moves.reserve(150);
    }

    void startPgn() override { current_moves.clear(); }

    void header(std::string_view key, std::string_view value) override {}
    void startMoves() override {}

    void move(std::string_view move_str, std::string_view comment) override {
        if (current_index >= skip_target) {
            // 必须拷贝 string_view，因为 parser buffer 会变
            current_moves.emplace_back(move_str);
        }
    }

    void endPgn() override {
        if (current_index >= skip_target) {
            GameTask task;
            task.moves = std::move(current_moves);
            // Push 到队列，如果满则阻塞
            queue.push(std::move(task));
            read_counter++;

            // 重新保留空间，避免频繁分配
            current_moves = std::vector<std::string>();
            current_moves.reserve(150);
        } else {
            if (current_index % 50000 == 0) {
                std::cout << "[Skipping] " << current_index << " / "
                          << skip_target << "\r" << std::flush;
            }
        }
        current_index++;
    }
};

// ================= Main Tools =================

void save_json(const GlobalStats &stats, int processed_count,
               const std::string &filename) {
    // 转换为 ordered map
    std::map<std::string, std::map<std::string, std::vector<int>>>
        ordered_matrix;
    for (const auto &[atk, victims] : stats.kill_matrix) {
        for (const auto &[vic, turns] : victims) {
            ordered_matrix[atk][vic] = turns;
        }
    }
    std::map<std::string, std::vector<int>> ordered_timeline;
    for (const auto &[vic, turns] : stats.death_timeline) {
        ordered_timeline[vic] = turns;
    }

    json j;
    j["games_processed"] = processed_count;
    j["stats"]["kill_matrix"] = ordered_matrix;
    j["stats"]["death_timeline"] = ordered_timeline;

    std::string temp_path = filename + ".tmp";
    std::ofstream o(temp_path);
    if (filename == CHECKPOINT_FILE) {
        o << j; // Checkpoint 不缩进
    } else {
        o << j.dump(4); // Final 结果缩进
    }
    o.close();
    try {
        fs::rename(temp_path, filename);
    } catch (...) {
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (!fs::exists(OUTCOME_DIR))
        fs::create_directories(OUTCOME_DIR);

    // 1. Load Checkpoint
    GlobalStats main_stats;
    int start_games = 0;

    if (fs::exists(CHECKPOINT_FILE)) {
        std::cout << "Loading checkpoint..." << std::endl;
        try {
            std::ifstream i(CHECKPOINT_FILE);
            json j;
            i >> j;
            start_games = j["games_processed"];

            // Restore logic
            if (j.contains("stats")) {
                if (j["stats"].contains("kill_matrix")) {
                    auto j_matrix = j["stats"]["kill_matrix"];
                    for (auto it = j_matrix.begin(); it != j_matrix.end();
                         ++it) {
                        std::string atk = it.key();
                        for (auto it2 = it.value().begin();
                             it2 != it.value().end(); ++it2) {
                            main_stats.kill_matrix[atk][it2.key()] =
                                it2.value().get<std::vector<int>>();
                        }
                    }
                }
                if (j["stats"].contains("death_timeline")) {
                    auto j_timeline = j["stats"]["death_timeline"];
                    for (auto it = j_timeline.begin(); it != j_timeline.end();
                         ++it) {
                        main_stats.death_timeline[it.key()] =
                            it.value().get<std::vector<int>>();
                    }
                }
            }
            std::cout << "Resuming from Game: " << start_games << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "Checkpoint error, starting fresh." << std::endl;
            start_games = 0;
            main_stats = GlobalStats();
        }
    }

    // 2. Setup Threading
    ThreadSafeQueue<GameTask> task_queue(MAX_QUEUE_SIZE);
    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<GameProcessor>> processors;

    std::atomic<int> games_processed{
        start_games};                         // 总共处理完的局数（包含跳过的）
    std::atomic<int> games_read{start_games}; // 从文件读取并Push到队列的局数
    std::atomic<int> active_workers{0};

    // 启动 Workers
    std::cout << "Launching " << NUM_THREADS << " worker threads..."
              << std::endl;
    for (int i = 0; i < NUM_THREADS; ++i) {
        auto processor = std::make_unique<GameProcessor>();
        processors.push_back(std::move(processor));

        workers.emplace_back([&, i]() {
            active_workers++;
            GameProcessor *my_proc = processors[i].get();
            GameTask task;
            while (task_queue.pop(task)) {
                my_proc->process_game(task);
                games_processed++;
            }
            active_workers--;
        });
    }

    // 3. Start Reader (Main Thread)
    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "Cannot open PGN file." << std::endl;
        return 1;
    }

    DispatcherVisitor visitor(task_queue, games_read, start_games);
    chess::pgn::StreamParser<1024> parser(pgn_file); // 1MB Buffer

    auto start_time = std::chrono::steady_clock::now();
    auto last_save_time = start_time;

    // 监控线程：负责打印进度和定期保存
    std::thread monitor_thread([&]() {
        while (active_workers > 0 || games_read == 0) { // 简单保活逻辑
            std::this_thread::sleep_for(std::chrono::seconds(1));

            int done = games_processed.load();
            int queued = task_queue.size();

            auto now = std::chrono::steady_clock::now();
            double elapsed =
                std::chrono::duration<double>(now - start_time).count();
            double speed = (elapsed > 0) ? (done - start_games) / elapsed : 0;

            // 打印进度
            std::cout << "\r[Processed: " << done << "] [Queue: " << queued
                      << "] [Speed: " << std::fixed << std::setprecision(1)
                      << speed << " g/s]   " << std::flush;

            // 定期保存 Checkpoint
            // 注意：这里保存的是"快照"，为了线程安全，我们不直接操作 worker 的
            // stats 只有当程序结束时才做全量 Merge。Checkpoint
            // 这里我们做一个简化：
            // 仅仅记录进度，或者如果非要保存数据，需要加锁暂停 workers
            // (太复杂)。 权衡：Checkpoint 记录 games_processed 数量。
            // 真实数据存在内存里，如果Crash，丢掉本次运行的数据。
            // 为了安全，每隔一段时间执行一次 "Stop-the-world" Merge。

            if (done - start_games > 0 && done % SAVE_INTERVAL == 0) {
                // 简单的 Checkpoint 只保存进度数字，不保存巨大矩阵，防止IO阻塞
                // 只有最终结束才保存完整矩阵，或者我们可以做一个Copy逻辑
                // 这里为了性能，我们假设机器稳定，只在最后保存。
                // 如果需要中间保存，需要在这里遍历 processors 并 merge 到
                // temp_stats
            }

            if (active_workers == 0 && task_queue.size() == 0)
                break;
        }
    });

    std::cout << "Starting PGN parsing..." << std::endl;
    try {
        parser.readGames(visitor);
    } catch (const std::exception &e) {
        std::cerr << "Parsing error: " << e.what() << std::endl;
    }

    // 解析结束，通知 Workers 停止
    std::cout << "\nParsing finished. Waiting for workers..." << std::endl;
    task_queue.stop();

    for (auto &t : workers) {
        if (t.joinable())
            t.join();
    }
    if (monitor_thread.joinable())
        monitor_thread.join();

    // 4. Merge Results
    std::cout << "Merging thread results..." << std::endl;
    for (const auto &proc : processors) {
        main_stats.merge_from(proc->get_stats());
    }

    // 5. Final Save
    std::cout << "Saving final results..." << std::endl;
    save_json(main_stats, games_processed, FINAL_RESULT_FILE);
    // 同时更新 Checkpoint
    save_json(main_stats, games_processed, CHECKPOINT_FILE);

    std::cout << "Done." << std::endl;
    return 0;
}