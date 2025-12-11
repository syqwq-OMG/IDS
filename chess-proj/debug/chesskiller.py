import chess
import chess.pgn
import json
import os
import time
from collections import defaultdict

# ================= 配置区域 =================
# PGN_FILE_PATH = "dataset/lichess_db_standard_rated_2025-11.pgn"
PGN_FILE_PATH = "../dataset/experiment.pgn"
OUTCOME_DIR = "../outcome/chess_killer"
CHECKPOINT_FILE = os.path.join(OUTCOME_DIR, "checkpoint_v3_test.json")
FINAL_RESULT_FILE = os.path.join(OUTCOME_DIR, "final_matrix_py.json")

# 统计上限回合数 (索引0-74对应1-75回合，索引75对应76+回合)
MAX_TURN_INDEX = 75 
SAVE_INTERVAL = 10000  # 每处理多少局保存一次
# ===========================================

class AdvancedTracker:
    def __init__(self):
        self.board_map = [None] * 64
        self.initialize_ids()

    def initialize_ids(self):
        files = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h']
        # 初始化逻辑与之前相同，确保每个棋子有唯一ID
        # White
        self.board_map[chess.A1] = {"id": "White_Rook_a", "promoted": False}
        self.board_map[chess.B1] = {"id": "White_Knight_b", "promoted": False}
        self.board_map[chess.C1] = {"id": "White_Bishop_c", "promoted": False}
        self.board_map[chess.D1] = {"id": "White_Queen", "promoted": False}
        self.board_map[chess.E1] = {"id": "White_King", "promoted": False}
        self.board_map[chess.F1] = {"id": "White_Bishop_f", "promoted": False}
        self.board_map[chess.G1] = {"id": "White_Knight_g", "promoted": False}
        self.board_map[chess.H1] = {"id": "White_Rook_h", "promoted": False}
        for i, f in enumerate(files):
            self.board_map[chess.A2 + i] = {"id": f"White_Pawn_{f}", "promoted": False}

        # Black
        self.board_map[chess.A8] = {"id": "Black_Rook_a", "promoted": False}
        self.board_map[chess.B8] = {"id": "Black_Knight_b", "promoted": False}
        self.board_map[chess.C8] = {"id": "Black_Bishop_c", "promoted": False}
        self.board_map[chess.D8] = {"id": "Black_Queen", "promoted": False}
        self.board_map[chess.E8] = {"id": "Black_King", "promoted": False}
        self.board_map[chess.F8] = {"id": "Black_Bishop_f", "promoted": False}
        self.board_map[chess.G8] = {"id": "Black_Knight_g", "promoted": False}
        self.board_map[chess.H8] = {"id": "Black_Rook_h", "promoted": False}
        for i, f in enumerate(files):
            self.board_map[chess.A7 + i] = {"id": f"Black_Pawn_{f}", "promoted": False}

    def get_full_id(self, piece_data):
        if piece_data is None: return None
        suffix = "_promoted" if piece_data['promoted'] else ""
        return f"{piece_data['id']}{suffix}"

def get_turn_index(board_fullmove):
    # 回合数从1开始。Index 0 对应 Turn 1.
    # 如果超过75回合，归入 index 75
    idx = board_fullmove - 1
    if idx < 0: idx = 0 # 防御性编程
    if idx > MAX_TURN_INDEX: idx = MAX_TURN_INDEX
    return idx

def update_stats(stats, attacker_id, victim_id, turn_idx):
    """
    更新全局统计数据 (3D Matrix)
    """
    # 1. 更新 Kill Matrix: attacker -> victim -> turn
    if attacker_id not in stats["kill_matrix"]:
        stats["kill_matrix"][attacker_id] = {}
    
    if victim_id not in stats["kill_matrix"][attacker_id]:
        # 初始化一个长度为 MAX_TURN_INDEX + 1 的数组
        stats["kill_matrix"][attacker_id][victim_id] = [0] * (MAX_TURN_INDEX + 1)
    
    stats["kill_matrix"][attacker_id][victim_id][turn_idx] += 1

    # 2. 更新 Death Timeline: victim -> turn
    if victim_id not in stats["death_timeline"]:
        stats["death_timeline"][victim_id] = [0] * (MAX_TURN_INDEX + 1)
    
    stats["death_timeline"][victim_id][turn_idx] += 1

def process_game_moves(game, stats):
    board = game.board()
    tracker = AdvancedTracker()

    for move in game.mainline_moves():
        from_sq = move.from_square
        to_sq = move.to_square
        
        attacker_data = tracker.board_map[from_sq]
        if attacker_data is None: 
            board.push(move)
            continue # 数据异常跳过

        attacker_id = tracker.get_full_id(attacker_data)
        turn_idx = get_turn_index(board.fullmove_number)

        # --- 1. 处理吃子 (Physical Capture) ---
        if board.is_capture(move):
            victim_data = None
            
            if board.is_en_passant(move):
                # 吃过路兵：受害者在旁边
                offset = -8 if board.turn == chess.WHITE else 8
                victim_sq = to_sq + offset
                victim_data = tracker.board_map[victim_sq]
                # 移除被吃掉的兵
                tracker.board_map[victim_sq] = None
            else:
                # 普通吃子
                victim_data = tracker.board_map[to_sq]

            if victim_data:
                victim_id = tracker.get_full_id(victim_data)
                update_stats(stats, attacker_id, victim_id, turn_idx)

        # --- 2. 移动 Tracker 逻辑 ---
        # 处理易位
        if board.is_castling(move):
            if to_sq == chess.G1:   # White K-side
                tracker.board_map[chess.F1] = tracker.board_map[chess.H1]
                tracker.board_map[chess.H1] = None
            elif to_sq == chess.C1: # White Q-side
                tracker.board_map[chess.D1] = tracker.board_map[chess.A1]
                tracker.board_map[chess.A1] = None
            elif to_sq == chess.G8: # Black K-side
                tracker.board_map[chess.F8] = tracker.board_map[chess.H8]
                tracker.board_map[chess.H8] = None
            elif to_sq == chess.C8: # Black Q-side
                tracker.board_map[chess.D8] = tracker.board_map[chess.A8]
                tracker.board_map[chess.A8] = None
        
        # 移动棋子
        tracker.board_map[to_sq] = attacker_data
        tracker.board_map[from_sq] = None

        # 处理升变
        if move.promotion:
            tracker.board_map[to_sq]['promoted'] = True

        # 在逻辑棋盘落子 (必须先 update tracker 再 push，否则判断过路兵位置会麻烦)
        board.push(move)

        # --- 3. 处理将杀 (Checkmate as a Kill) ---
        if board.is_checkmate():
            # 此时 attacker_data 已经在 to_sq (如果不是升变)
            # 注意：如果刚才这步是升变，Attacker ID 应该变成 promoted 吗？
            # 逻辑上：兵到底线变后后将杀，应该是 "White_Pawn_a_promoted" 杀了王。
            # 我们重新获取一次 id，确保状态最新
            final_attacker_data = tracker.board_map[to_sq]
            final_attacker_id = tracker.get_full_id(final_attacker_data)
            
            # 受害者是对方的王
            opponent_king_color = "Black" if board.turn == chess.WHITE else "White" 
            # board.turn 在 push 后已经变了，如果现在是 White 走，说明刚才 Black 杀的
            # 哎呀，board.push(move) 之后，轮次已经交给对方了。
            # 如果 board.is_checkmate() 为真，说明当前轮次方无法动弹，被上一手方杀死了。
            
            loser_color = "White" if board.turn == chess.WHITE else "Black"
            victim_id = f"{loser_color}_King" 
            
            # 记录虚空击杀
            update_stats(stats, final_attacker_id, victim_id, turn_idx)

def run_advanced_analysis():
    if not os.path.exists(OUTCOME_DIR):
        os.makedirs(OUTCOME_DIR)

    # 数据结构初始化
    stats = {
        "kill_matrix": {},    # 3D: Attacker -> Victim -> [Turn Counts]
        "death_timeline": {}  # 2D: Victim -> [Turn Counts]
    }
    games_processed = 0
    file_offset = 0

    # 断点续传
    if os.path.exists(CHECKPOINT_FILE):
        print(f"Loading checkpoint: {CHECKPOINT_FILE}")
        try:
            with open(CHECKPOINT_FILE, 'r') as f:
                data = json.load(f)
                stats = data.get("stats", stats)
                games_processed = data.get("games_processed", 0)
                file_offset = data.get("current_pgn_offset", 0)
            print(f"Resuming from Game {games_processed}, Offset {file_offset}")
        except Exception as e:
            print(f"Checkpoint load failed: {e}. Starting fresh.")

    if not os.path.exists(PGN_FILE_PATH):
        print("PGN file not found.")
        return

    print("Starting Advanced Analysis...")
    start_time = time.time()

    with open(PGN_FILE_PATH) as pgn_file:
        pgn_file.seek(file_offset)
        
        while True:
            try:
                # 记录这局开始前的偏移量，以便保存
                offset_before_game = pgn_file.tell()
                game = chess.pgn.read_game(pgn_file)
            except Exception:
                continue

            if game is None:
                break

            process_game_moves(game, stats)
            games_processed += 1

            if games_processed % SAVE_INTERVAL == 0:
                elapsed = time.time() - start_time
                print(f"Processed: {games_processed} | Time: {elapsed:.1f}s | Speed: {games_processed/elapsed:.1f} g/s")
                
                # 获取读完后的 offset
                current_offset = pgn_file.tell()
                
                checkpoint_data = {
                    "games_processed": games_processed,
                    "current_pgn_offset": current_offset,
                    "stats": stats
                }
                
                # 原子写入
                temp_path = CHECKPOINT_FILE + ".tmp"
                with open(temp_path, 'w') as f:
                    json.dump(checkpoint_data, f) # 这里不indent以节省空间和IO
                os.replace(temp_path, CHECKPOINT_FILE)

    # Final Save
    with open(FINAL_RESULT_FILE, 'w') as f:
        json.dump(stats, f, indent=4) # 最终结果格式化以便人类阅读
    print(f"Done! Saved to {FINAL_RESULT_FILE}")

if __name__ == "__main__":
    run_advanced_analysis()