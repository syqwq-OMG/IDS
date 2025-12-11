#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <cmath>
#include <iomanip>
#include <filesystem>

// 引入依赖库
#include "chess.hpp"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "../dataset/lichess_db_standard_rated_2025-11.pgn"; 
const std::string OUTCOME_DIR = "../output/chess_resign"; 
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/final_resignation_stats.json";
const std::string CHECKPOINT_FILE = OUTCOME_DIR + "/checkpoint_resign.json";

const int WINDOW_SIZE = 5; 
const int SAVE_INTERVAL = 50000; // 每处理 50,000 局保存一次 Checkpoint
const size_t PARSER_BUFFER_SIZE = 1024; // 1MB Parser Buffer
// ===========================================

using ResignationMatrix = std::map<int, std::map<int, int>>;

// 核心统计结构
struct ResignationStats {
    ResignationMatrix white_resigned;
    ResignationMatrix black_resigned;
    long long games_processed = 0;
    long long resignation_games_found = 0;
    std::streampos current_offset = 0; // 记录文件读取位置

    // 将数据转换为 JSON 对象
    json to_json() const {
        json j;
        j["games_processed"] = games_processed;
        j["resignation_games_found"] = resignation_games_found;
        j["current_offset"] = (long long)current_offset; // streampos 转 long long

        // Map 转 JSON (Key 必须是 String)
        auto& j_white = j["white_resigned"];
        for (const auto& [turn, diff_map] : white_resigned) {
            for (const auto& [diff, count] : diff_map) {
                j_white[std::to_string(turn)][std::to_string(diff)] = count;
            }
        }

        auto& j_black = j["black_resigned"];
        for (const auto& [turn, diff_map] : black_resigned) {
            for (const auto& [diff, count] : diff_map) {
                j_black[std::to_string(turn)][std::to_string(diff)] = count;
            }
        }
        return j;
    }

    // 从 JSON 对象加载数据
    void from_json(const json& j) {
        games_processed = j.value("games_processed", 0LL);
        resignation_games_found = j.value("resignation_games_found", 0LL);
        current_offset = (std::streampos)j.value("current_offset", 0LL);

        if (j.contains("white_resigned")) {
            for (auto& [turn_str, diff_map_j] : j["white_resigned"].items()) {
                int turn = std::stoi(turn_str);
                for (auto& [diff_str, count] : diff_map_j.items()) {
                    white_resigned[turn][std::stoi(diff_str)] = count.get<int>();
                }
            }
        }

        if (j.contains("black_resigned")) {
            for (auto& [turn_str, diff_map_j] : j["black_resigned"].items()) {
                int turn = std::stoi(turn_str);
                for (auto& [diff_str, count] : diff_map_j.items()) {
                    black_resigned[turn][std::stoi(diff_str)] = count.get<int>();
                }
            }
        }
    }
};

// ---------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------
bool is_checkmate(chess::Board& board) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    return moves.empty() && board.inCheck();
}

double calculate_material_score(const chess::Board& board, chess::Color color) {
    double score = 0.0;
    score += board.pieces(chess::PieceType::QUEEN, color).count() * 9.0;
    score += board.pieces(chess::PieceType::ROOK, color).count() * 5.0;
    score += board.pieces(chess::PieceType::BISHOP, color).count() * 3.0;
    score += board.pieces(chess::PieceType::KNIGHT, color).count() * 3.0;

    chess::Bitboard pawns = board.pieces(chess::PieceType::PAWN, color);
    while (pawns) {
        auto sq = pawns.pop();
        int rank = static_cast<int>(sq) / 8;
        if ((color == chess::Color::WHITE && rank == 6) || (color == chess::Color::BLACK && rank == 1)) {
            score += 2.0; 
        } else {
            score += 1.0;
        }
    }
    return score;
}

// ---------------------------------------------------------
// Visitor 类
// ---------------------------------------------------------
class ResignationVisitor : public chess::pgn::Visitor {
    ResignationStats& stats;
    chess::Board board;
    std::ifstream& pgn_stream; // 引用文件流以获取 offset
    
    std::deque<double> material_diff_window;
    std::string current_result;

public:
    ResignationVisitor(ResignationStats& s, std::ifstream& fs) 
        : stats(s), pgn_stream(fs) {
        board.setFen(chess::constants::STARTPOS);
    }

    void startPgn() override {
        board.setFen(chess::constants::STARTPOS);
        material_diff_window.clear();
        material_diff_window.push_back(0.0);
        current_result = "*";
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "Result") current_result = std::string(value);
    }

    void startMoves() override {}

    void move(std::string_view move_str, std::string_view comment) override {
        (void)comment;
        chess::Move move = chess::uci::parseSan(board, move_str);
        board.makeMove(move);

        double w_score = calculate_material_score(board, chess::Color::WHITE);
        double b_score = calculate_material_score(board, chess::Color::BLACK);
        double diff = w_score - b_score; 

        material_diff_window.push_back(diff);
        if (material_diff_window.size() > WINDOW_SIZE) {
            material_diff_window.pop_front();
        }
    }

    void endPgn() override {
        stats.games_processed++;

        if (current_result != "1/2-1/2" && current_result != "*") {
            if (!is_checkmate(board)) {
                stats.resignation_games_found++;
                
                if (!material_diff_window.empty()) {
                    double sum_diff = 0.0;
                    for (double d : material_diff_window) sum_diff += d;
                    double avg_diff = sum_diff / material_diff_window.size();

                    int diff_key = static_cast<int>(std::round(avg_diff * 10));
                    int turn_number = board.fullMoveNumber(); 

                    if (current_result == "1-0") {
                        stats.black_resigned[turn_number][diff_key]++;
                    } else if (current_result == "0-1") {
                        stats.white_resigned[turn_number][diff_key]++;
                    }
                }
            }
        }

        // --- 断点保存逻辑 ---
        if (stats.games_processed % SAVE_INTERVAL == 0) {
            save_checkpoint();
        }
    }

    // 保存 Checkpoint 到磁盘
    void save_checkpoint() {
        // 更新当前的 offset
        // 注意：由于 Parser 有 Buffer，tellg() 指向的是 Buffer 末尾。
        // 直接使用 tellg() 可能会在恢复时略微超前，但在海量数据统计中，
        // 只要不是经常崩溃，这点误差（<1MB 数据）通常可以接受。
        // 如果追求极致精确，需要 hack Parser 或使用 unbuffered IO，会严重拖慢速度。
        stats.current_offset = pgn_stream.tellg();

        json j = stats.to_json();
        
        // 原子写入：先写临时文件，再重命名
        std::string temp_path = CHECKPOINT_FILE + ".tmp";
        std::ofstream out(temp_path);
        if (out.is_open()) {
            out << j.dump(); // 压缩格式存储以节省空间
            out.close();
            fs::rename(temp_path, CHECKPOINT_FILE);
            
            std::cout << "[Checkpoint] Saved at Game " << stats.games_processed 
                      << " (Resigns: " << stats.resignation_games_found << ")" << std::endl;
        } else {
            std::cerr << "[Error] Failed to write checkpoint!" << std::endl;
        }
    }
};

// ---------------------------------------------------------
// 主函数
// ---------------------------------------------------------
int main() {
    // 1. 准备目录
    if (!fs::exists(OUTCOME_DIR)) {
        fs::create_directories(OUTCOME_DIR);
    }

    ResignationStats stats;
    
    // 2. 尝试加载 Checkpoint
    if (fs::exists(CHECKPOINT_FILE)) {
        std::cout << "Found checkpoint file. Loading..." << std::endl;
        try {
            std::ifstream ckpt_in(CHECKPOINT_FILE);
            json j_ckpt;
            ckpt_in >> j_ckpt;
            stats.from_json(j_ckpt);
            std::cout << "Resuming from Game: " << stats.games_processed 
                      << " | Offset: " << stats.current_offset << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading checkpoint: " << e.what() << ". Starting fresh." << std::endl;
            stats = ResignationStats(); // 重置
        }
    }

    // 3. 打开 PGN 文件
    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "Error: PGN file not found at " << PGN_FILE_PATH << std::endl;
        return 1;
    }

    // 4. 跳转到断点位置
    if (stats.current_offset > 0) {
        pgn_file.seekg(stats.current_offset);
        if (pgn_file.fail()) {
            std::cerr << "Error seeking to offset. Restarting file." << std::endl;
            pgn_file.clear();
            pgn_file.seekg(0);
        }
    }

    std::cout << "Starting Analysis..." << std::endl;

    // 5. 初始化并运行解析器
    ResignationVisitor visitor(stats, pgn_file);
    // 使用 1MB Buffer
    chess::pgn::StreamParser<PARSER_BUFFER_SIZE> parser(pgn_file); 
    
    try {
        parser.readGames(visitor);
    } catch (const std::exception& e) {
        std::cerr << "\n[Exception] Parsing stopped: " << e.what() << std::endl;
        // 崩溃前尝试保存一次
        visitor.save_checkpoint();
        return 1;
    }

    // 6. 完成，保存最终结果
    std::cout << "\nAnalysis Complete." << std::endl;
    
    // 保存最终 JSON
    json j_root;
    j_root["meta_info"] = {
        {"source_file", PGN_FILE_PATH},
        {"total_games", stats.games_processed},
        {"resignation_count", stats.resignation_games_found},
        {"window_size", WINDOW_SIZE},
        {"note", "material_diff scaled by 10"}
    };

    // 使用 helper 函数生成的 json 内容填充
    json stats_json = stats.to_json();
    j_root["white_resigned"] = stats_json["white_resigned"];
    j_root["black_resigned"] = stats_json["black_resigned"];

    std::ofstream out_file(FINAL_RESULT_FILE);
    out_file << std::setw(4) << j_root << std::endl;

    std::cout << "Results saved to: " << FINAL_RESULT_FILE << std::endl;
    
    // 删除 checkpoint 文件（可选，这里保留以防万一，或者手动删除）
    // fs::remove(CHECKPOINT_FILE); 

    return 0;
}