/*
 * fathom.cpp
 * (C) 2015 basil, all rights reserved.
 * (C) 2018-2019 Jon Dart, All Rights Reserved.
 * (C) 2023 Viktor Molnár, All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define DLL_EXPORT __declspec(dllexport)

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <windows.h>

#include "tbprobe.h"

#define BOARD_RANK_1            0x00000000000000FFull
#define BOARD_FILE_A            0x8080808080808080ull
#define square(r, f)            (8 * (r) + (f))
#define rank(s)                 ((s) >> 3)
#define file(s)                 ((s) & 0x07)
#define board(s)                ((uint64_t)1 << (s))

static const char *wdl_to_str[5] =
{
    "0-1",
    "1/2-1/2",
    "1/2-1/2",
    "1/2-1/2",
    "1-0"
};

struct pos
{
    uint64_t white;
    uint64_t black;
    uint64_t kings;
    uint64_t queens;
    uint64_t rooks;
    uint64_t bishops;
    uint64_t knights;
    uint64_t pawns;
    uint8_t castling;
    uint8_t rule50;
    uint8_t ep;
    bool turn;
    uint16_t move;
};

/*
 * Parse a FEN string.
 */
static bool parse_FEN(struct pos *pos, const char *fen)
{
    uint64_t white = 0, black = 0;
    uint64_t kings, queens, rooks, bishops, knights, pawns;
    kings = queens = rooks = bishops = knights = pawns = 0;
    bool turn;
    unsigned rule50 = 0, move = 1;
    unsigned ep = 0;
    unsigned castling = 0;
    char c;
    int r, f;

    if (fen == NULL)
        goto fen_parse_error;

    for (r = 7; r >= 0; r--)
    {
        for (f = 0; f <= 7; f++)
        {
            unsigned s = (r * 8) + f;
            uint64_t b = board(s);
            c = *fen++;
            switch (c)
            {
                case 'k':
                    kings |= b;
                    black |= b;
                    continue;
                case 'K':
                    kings |= b;
                    white |= b;
                    continue;
                case 'q':
                    queens |= b;
                    black |= b;
                    continue;
                case 'Q':
                    queens |= b;
                    white |= b;
                    continue;
                case 'r':
                    rooks |= b;
                    black |= b;
                    continue;
                case 'R':
                    rooks |= b;
                    white |= b;
                    continue;
                case 'b':
                    bishops |= b;
                    black |= b;
                    continue;
                case 'B':
                    bishops |= b;
                    white |= b;
                    continue;
                case 'n':
                    knights |= b;
                    black |= b;
                    continue;
                case 'N':
                    knights |= b;
                    white |= b;
                    continue;
                case 'p':
                    pawns |= b;
                    black |= b;
                    continue;
                case 'P':
                    pawns |= b;
                    white |= b;
                    continue;
                default:
                    break;
            }
            if (c >= '1' && c <= '8')
            {
                unsigned jmp = (unsigned)c - '0';
                f += jmp-1;
                continue;
            }
            goto fen_parse_error;
        }
        if (r == 0)
            break;
        c = *fen++;
        if (c != '/')
            goto fen_parse_error;
    }
    c = *fen++;
    if (c != ' ')
        goto fen_parse_error;
    c = *fen++;
    if (c != 'w' && c != 'b')
        goto fen_parse_error;
    turn = (c == 'w');
    c = *fen++;
    if (c != ' ')
        goto fen_parse_error;
    c = *fen++;
    if (c != '-')
    {
        do
        {
            switch (c)
            {
                case 'K':
                    castling |= TB_CASTLING_K; break;
                case 'Q':
                    castling |= TB_CASTLING_Q; break;
                case 'k':
                    castling |= TB_CASTLING_k; break;
                case 'q':
                    castling |= TB_CASTLING_q; break;
                default:
                    goto fen_parse_error;
            }
            c = *fen++;
        }
        while (c != ' ');
        fen--;
    }
    c = *fen++;
    if (c != ' ')
        goto fen_parse_error;
    c = *fen++;
    if (c >= 'a' && c <= 'h')
    {
        unsigned file = c - 'a';
        c = *fen++;
        if (c != '3' && c != '6')
            goto fen_parse_error;
        unsigned rank = c - '1';
        ep = square(rank, file);
        if (rank == 2 && turn)
            goto fen_parse_error;
        if (rank == 5 && !turn)
            goto fen_parse_error;
        if (rank == 2 && ((tb_pawn_attacks(ep, true) & (black & pawns)) == 0))
            ep = 0;
        if (rank == 5 && ((tb_pawn_attacks(ep, false) & (white & pawns)) == 0))
            ep = 0;
    }
    else if (c != '-')
        goto fen_parse_error;
    c = *fen++;
    if (c != ' ')
        goto fen_parse_error;
    char clk[4];
    clk[0] = *fen++;
    if (clk[0] < '0' || clk[0] > '9')
        goto fen_parse_error;
    clk[1] = *fen++;
    if (clk[1] != ' ')
    {
        if (clk[1] < '0' || clk[1] > '9')
            goto fen_parse_error;
        clk[2] = *fen++;
        if (clk[2] != ' ')
        {
            if (clk[2] < '0' || clk[2] > '9')
                goto fen_parse_error;
            c = *fen++;
            if (c != ' ')
                goto fen_parse_error;
            clk[3] = '\0';
        }
        else
            clk[2] = '\0';
    }
    else
        clk[1] = '\0';
    rule50 = atoi(clk);
    move = atoi(fen);

    pos->white = white;
    pos->black = black;
    pos->kings = kings;
    pos->queens = queens;
    pos->rooks = rooks;
    pos->bishops = bishops;
    pos->knights = knights;
    pos->pawns = pawns;
    pos->castling = castling;
    pos->rule50 = rule50;
    pos->ep = ep;
    pos->turn = turn;
    pos->move = move;
    return true;

fen_parse_error:
    return false;
}

/*
 * Test if the given move is an en passant capture.
 */
static bool is_en_passant(const struct pos *pos, uint64_t from, uint64_t to)
{
    uint64_t us = (pos->turn? pos->white: pos->black);
    if (pos->ep == 0)
        return false;
    if (to != pos->ep)
        return false;
    if ((board(from) & us & pos->pawns) == 0)
        return false;
    return true;
}

/*
 * Test if the king is in check.
 */
static bool is_check(const struct pos *pos)
{
    uint64_t occ = pos->white | pos->black;
    uint64_t us = (pos->turn? pos->white: pos->black),
             them = (pos->turn? pos->black: pos->white);
    uint64_t king = pos->kings & us;
    unsigned sq = tb_lsb(king);
    uint64_t ratt = tb_rook_attacks(sq, occ);
    uint64_t batt = tb_bishop_attacks(sq, occ);
    if (ratt & (pos->rooks & them))
        return true;
    if (batt & (pos->bishops & them))
        return true;
    if ((ratt | batt) & (pos->queens & them))
        return true;
    if (tb_knight_attacks(sq) & (pos->knights & them))
        return true;
    if (tb_pawn_attacks(sq, pos->turn) & (pos->pawns & them))
        return true;
    return false;
}

/*
 * Convert a move into a string.
 */
static void move_parts_to_str(const struct pos *pos, int from, int to, int promotes, char *str) {
    unsigned r        = rank(from);
    unsigned f        = file(from);
    uint64_t occ      = pos->black | pos->white;
    uint64_t us       = (pos->turn? pos->white: pos->black);
    bool     capture  = (occ & board(to)) != 0 || is_en_passant(pos,from,to);
    uint64_t b = board(from), att = 0;
    if (b & pos->kings)
        *str++ = 'K';
    else if (b & pos->queens)
    {
        *str++ = 'Q';
        att = tb_queen_attacks(to, occ) & us & pos->queens;
    }
    else if (b & pos->rooks)
    {
        *str++ = 'R';
        att = tb_rook_attacks(to, occ) & us & pos->rooks;
    }
    else if (b & pos->bishops)
    {
        *str++ = 'B';
        att = tb_bishop_attacks(to, occ) & us & pos->bishops;
    }
    else if (b & pos->knights)
    {
        *str++ = 'N';
        att = tb_knight_attacks(to) & us & pos->knights;
    }
    else
        att = tb_pawn_attacks(to, !pos->turn) & us & pos->pawns;
    if ((b & pos->pawns) && capture)
        *str++ = 'a' + f;
    else if (tb_pop_count(att) > 1)
    {
        if (tb_pop_count(att & (BOARD_FILE_A >> f)) == 1)
            *str++ = 'a' + f;
        else if (tb_pop_count(att & (BOARD_RANK_1 << (8*r))) == 1)
            *str++ = '1' + r;
        else
        {
            *str++ = 'a' + f;
            *str++ = '1' + r;
        }
    }
    if (capture)
        *str++ = 'x';
    *str++ = 'a' + file(to);
    *str++ = '1' + rank(to);
    if (promotes != TB_PROMOTES_NONE)
    {
        *str++ = '=';
        switch (promotes)
        {
            case TB_PROMOTES_QUEEN:
                *str++ = 'Q'; break;
            case TB_PROMOTES_ROOK:
                *str++ = 'R'; break;
            case TB_PROMOTES_BISHOP:
                *str++ = 'B'; break;
            case TB_PROMOTES_KNIGHT:
                *str++ = 'N'; break;
        }
    }
    *str++ = '\0';
}

static void move_to_str(const struct pos *pos, unsigned move, char *str)
{
    unsigned from     = TB_GET_FROM(move);
    unsigned to       = TB_GET_TO(move);
    unsigned promotes = TB_GET_PROMOTES(move);
    move_parts_to_str(pos, from, to, promotes, str);
}

/*
 * Do a move.  Does not support castling.
 */
#define do_bb_move(b, from, to)                                         \
    (((b) & (~board(to)) & (~board(from))) |                            \
        ((((b) >> (from)) & 0x1) << (to)))
static void do_move(struct pos *pos, unsigned move)
{
    unsigned from     = TB_GET_FROM(move);
    unsigned to       = TB_GET_TO(move);
    unsigned promotes = TB_GET_PROMOTES(move);
    bool turn         = !pos->turn;
    uint64_t white    = do_bb_move(pos->white, from, to);
    uint64_t black    = do_bb_move(pos->black, from, to);
    uint64_t kings    = do_bb_move(pos->kings, from, to);
    uint64_t queens   = do_bb_move(pos->queens, from, to);
    uint64_t rooks    = do_bb_move(pos->rooks, from, to);
    uint64_t bishops  = do_bb_move(pos->bishops, from, to);
    uint64_t knights  = do_bb_move(pos->knights, from, to);
    uint64_t pawns    = do_bb_move(pos->pawns, from, to);
    unsigned ep       = 0;
    unsigned rule50   = pos->rule50;
    if (promotes != TB_PROMOTES_NONE)
    {
        pawns &= ~board(to);
        switch (promotes)
        {
            case TB_PROMOTES_QUEEN:
                queens |= board(to); break;
            case TB_PROMOTES_ROOK:
                rooks |= board(to); break;
            case TB_PROMOTES_BISHOP:
                bishops |= board(to); break;
            case TB_PROMOTES_KNIGHT:
                knights |= board(to); break;
        }
        rule50 = 0;
    }
    else if ((board(from) & pos->pawns) != 0)
    {
        rule50 = 0;
        if (rank(from) == 1 && rank(to) == 3 &&
            (tb_pawn_attacks(from+8, true) & pos->pawns & pos->black) != 0)
            ep = from+8;
        else if (rank(from) == 6 && rank(to) == 4 &&
            (tb_pawn_attacks(from-8, false) & pos->pawns & pos->white) != 0)
            ep = from-8;
        else if (TB_GET_EP(move))
        {
            unsigned ep_to = (pos->turn? to-8: to+8);
            uint64_t ep_mask = ~board(ep_to);
            white &= ep_mask;
            black &= ep_mask;
            pawns &= ep_mask;
        }
    }
    else if ((board(to) & (pos->white | pos->black)) != 0)
        rule50 = 0;
    else
        rule50++;
    pos->white   = white;
    pos->black   = black;
    pos->kings   = kings;
    pos->queens  = queens;
    pos->rooks   = rooks;
    pos->bishops = bishops;
    pos->knights = knights;
    pos->pawns   = pawns;
    pos->ep      = ep;
    pos->rule50  = rule50;
    pos->turn    = turn;
    pos->move += turn;
}

/*
 * Print the pseudo "PV" for the given position.
 */
static void print_PV(struct pos *pos)
{
    struct pos temp = *pos;
    putchar('\n');
    bool first = true, check = false;
    if (!pos->turn)
    {
        first = false;
        printf("%u...", pos->move);
    }
    while (true)
    {
        unsigned move = tb_probe_root(pos->white, pos->black, pos->kings,
            pos->queens, pos->rooks, pos->bishops, pos->knights, pos->pawns,
            pos->rule50, pos->castling, pos->ep, pos->turn, NULL);
        if (move == TB_RESULT_FAILED)
        {
            printf("{TB probe failed}\n");
            break;
        }
        if (move == TB_RESULT_CHECKMATE)
        {
            printf("# %s\n", (pos->turn? "0-1": "1-0"));
            break;
        }
        if (check)
            putchar('+');
        if (pos->rule50 >= 100 || move == TB_RESULT_STALEMATE)
        {
            printf(" 1/2-1/2\n");
            break;
        }

        char str[32];
        move_to_str(pos, move, str);
        if (!first)
            putchar(' ');
        first = false;
        if (pos->turn)
            printf("%u. ", pos->move);
        printf("%s", str);
        do_move(pos, move);
        check = is_check(pos);
    }
    // restore to root position
    *pos = temp;
}

/*
 * Returns the best move available to us from the current position as "<FROM> <TO> <PROMOTES>"
 */
static const std::string best_move(struct pos *pos)
{
    struct pos temp = *pos;
    unsigned move = tb_probe_root(pos->white, pos->black, pos->kings,
            pos->queens, pos->rooks, pos->bishops, pos->knights, pos->pawns,
            pos->rule50, pos->castling, pos->ep, pos->turn, NULL);
    
    unsigned from     = TB_GET_FROM(move);
    unsigned to       = TB_GET_TO(move);
    unsigned promotes = TB_GET_PROMOTES(move);

    std::string str = "" + std::to_string(from) + " " + std::to_string(to) + " " + std::to_string(promotes);
    
    // restore to root position
    *pos = temp;
    return str;
}

/*
 * Print a list of moves that match the WDL value.
 */
static bool print_moves(struct pos *pos, unsigned *results, bool prev,
    unsigned wdl)
{
    for (unsigned i = 0; results[i] != TB_RESULT_FAILED; i++)
    {
        if (TB_GET_WDL(results[i]) != wdl)
            continue;
        if (prev)
            printf(", ");
        prev = true;
        char str[32];
        move_to_str(pos, results[i], str);
        printf("%s", str);
    }
    return prev;
}

/*
 * Converts the given string to BSTR
 *
 * Usage on C# side:
 * 
 * [DllImport(@"test.dll", CallingConvention = CallingConvention.Cdecl)]
 * [return: MarshalAs(UnmanagedType.BStr)]
 * private static extern string GetSomeText();
 * 
 */
BSTR ansi_to_bstr(std::string str)
{
    const char* input = str.c_str();
    BSTR result = NULL;
    int lenA = lstrlenA(input);
    int lenW = ::MultiByteToWideChar(CP_ACP, 0, input, lenA, NULL, 0);
    if (lenW > 0)
    {
        result = ::SysAllocStringLen(0, lenW);
        ::MultiByteToWideChar(CP_ACP, 0, input, lenA, result, lenW);
    } 
    return result;
}

/*
 * Initializes the tablebase with given path
 *
 * Returns whether the initialization was successful (true) or not (false)
 */
extern "C" DLL_EXPORT bool SetPath(char* path)
{
    // (0) init:
    if (path == NULL)
        path = getenv("TB_PATH");
    if (path == NULL) {
        fprintf(stderr, "Path not set");
        return false;
    }
    tb_init(path);
    if (TB_LARGEST == 0)
    {
        fprintf(stderr, "error: unable to initialize tablebase; no tablebase "
            "files found\n");
        return false;
    }
    return true;
}

/*
 * Returns the correct move to make in order to maximalize odds of winning
 */
extern "C" DLL_EXPORT BSTR SyzygyLookup(const char* fen)
{
    
    // (0) check if tablebase is initialized by SetPath
    if (TB_LARGEST == 0)
    {
        return ansi_to_bstr("error: tablebase is not initialized");
    }
    
    // (1) parse the FEN:
    struct pos pos0;
    struct pos *pos = &pos0;
    if (!parse_FEN(pos, fen))
    {
        return ansi_to_bstr("error: unable to parse FEN string");
    }

    // (2) probe the TB:
    if (tb_pop_count(pos->white | pos->black) > TB_LARGEST)
    {
        return ansi_to_bstr("error: unable to probe tablebase; FEN string has too many pieces");
    }
    unsigned results[TB_MAX_MOVES];
    unsigned res = tb_probe_root(pos->white, pos->black, pos->kings,
        pos->queens, pos->rooks, pos->bishops, pos->knights, pos->pawns,
        pos->rule50, pos->castling, pos->ep, pos->turn, results);
    if (res == TB_RESULT_FAILED)
    {
        return ansi_to_bstr("error: unable to probe tablebase; position invalid, illegal or not in tablebase");
    }

    // (3) Output:
    return ansi_to_bstr(best_move(pos));
}

