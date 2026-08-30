/* mermaid_svg.c — Hand-rolled Mermaid diagram-to-SVG engine.
 * Supports:
 *   1. Flowcharts / Graphs (graph TD/LR/RL, flowchart TD/LR/RL)
 *      with rect, rounded rect, stadium/pill, circle, subroutine, cylinder, and diamond nodes.
 *   2. Sequence Diagrams (sequenceDiagram) with participant lifelines and message arrows.
 *   3. Class Diagrams (classDiagram) with 3-compartment UML class boxes and relationships.
 *   4. Pie Charts (pie) with circular SVG arc paths, color swatches, and legend.
 *
 * Conforms to ISO C23 standard library.
 * Zero third-party dependencies.
 * All functions and structs are static to avoid single-TU linker collisions.
 */

#define MSVG_MAX_NODES 80
#define MSVG_MAX_EDGES 120
#define MSVG_MAX_BFS_DEPTH 100
#define MSVG_MAX_PARTICIPANTS 32
#define MSVG_MAX_MESSAGES 64
#define MSVG_MAX_CLASSES 32
#define MSVG_MAX_CLASS_MEMBERS 24
#define MSVG_MAX_PIE_SLICES 16

/* -------------------------------------------------------------------------
 * Basic Arithmetic & String Helpers
 * ------------------------------------------------------------------------- */
static float msvg_sin(float rad) {
    while (rad > 3.14159265f) rad -= 6.2831853f;
    while (rad < -3.14159265f) rad += 6.2831853f;
    float r2 = rad * rad;
    return rad * (1.0f - r2 / 6.0f * (1.0f - r2 / 20.0f * (1.0f - r2 / 42.0f)));
}

static float msvg_cos(float rad) {
    return msvg_sin(rad + 1.57079632679f);
}

static void msvg_trim(char *str) {
    if (!str) return;
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0') { *str = '\0'; return; }
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    if (start != str) memmove(str, start, (size_t)(end - start + 2));
}

static void msvg_str_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void msvg_svg_append(char **out, size_t *out_sz, size_t *out_len, const char *s) {
    size_t slen = strlen(s);
    if (*out_len + slen + 1 > *out_sz) {
        *out_sz = (*out_len + slen + 1) * 2;
        char *new_out = realloc(*out, *out_sz);
        if (!new_out) return;
        *out = new_out;
    }
    memcpy(*out + *out_len, s, slen);
    *out_len += slen;
    (*out)[*out_len] = '\0';
}

static void msvg_svg_append_fmt(char **out, size_t *out_sz, size_t *out_len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char tmp[1024];
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    msvg_svg_append(out, out_sz, out_len, tmp);
}

/* -------------------------------------------------------------------------
 * 1. FLOWCHART / GRAPH ENGINE
 * ------------------------------------------------------------------------- */
typedef enum {
    MSVG_NODE_RECT,
    MSVG_NODE_ROUND_RECT,
    MSVG_NODE_STADIUM,
    MSVG_NODE_CIRCLE,
    MSVG_NODE_SUBROUTINE,
    MSVG_NODE_CYLINDER,
    MSVG_NODE_DIAMOND
} MsvgNodeType;

typedef enum {
    MSVG_EDGE_ARROW,
    MSVG_EDGE_LINE
} MsvgEdgeType;

typedef enum {
    MSVG_DIR_TD,
    MSVG_DIR_LR,
    MSVG_DIR_RL
} MsvgFlowDir;

typedef struct {
    char id[64];
    char label[128];
    MsvgNodeType type;
    int depth;
    int rank_index;
    float x, y, w, h;
} MsvgNode;

typedef struct {
    int src_idx;
    int dst_idx;
    MsvgEdgeType type;
    char label[128];
} MsvgEdge;

typedef struct {
    MsvgNode nodes[MSVG_MAX_NODES];
    int node_count;
    MsvgEdge edges[MSVG_MAX_EDGES];
    int edge_count;
    MsvgFlowDir dir;
} MsvgGraph;

static int msvg_find_or_add_node(MsvgGraph *g, const char *id) {
    if (!id || !*id) return -1;
    for (int i = 0; i < g->node_count; i++) {
        if (strcmp(g->nodes[i].id, id) == 0) return i;
    }
    if (g->node_count >= MSVG_MAX_NODES) return -1;
    int idx = g->node_count++;
    msvg_str_copy(g->nodes[idx].id, sizeof(g->nodes[idx].id), id);
    msvg_str_copy(g->nodes[idx].label, sizeof(g->nodes[idx].label), id);
    g->nodes[idx].type = MSVG_NODE_RECT;
    g->nodes[idx].depth = -1;
    g->nodes[idx].rank_index = 0;
    g->nodes[idx].x = g->nodes[idx].y = 0;
    g->nodes[idx].w = 120; g->nodes[idx].h = 40;
    return idx;
}

static int msvg_add_flow_edge(MsvgGraph *g, int src, int dst, MsvgEdgeType type, const char *label) {
    if (src < 0 || dst < 0 || g->edge_count >= MSVG_MAX_EDGES) return -1;
    int idx = g->edge_count++;
    g->edges[idx].src_idx = src;
    g->edges[idx].dst_idx = dst;
    g->edges[idx].type = type;
    if (label) {
        msvg_str_copy(g->edges[idx].label, sizeof(g->edges[idx].label), label);
    } else {
        g->edges[idx].label[0] = '\0';
    }
    return idx;
}

static bool msvg_parse_flow_node(MsvgGraph *g, char *token, int *out_idx) {
    char id[64] = {0}, label[128] = {0};
    MsvgNodeType type = MSVG_NODE_RECT;

    char *open_mark = NULL;
    char *close_mark = NULL;

    /* 1. Stadium / Pill: ([Label]) */
    if ((open_mark = strstr(token, "([")) != NULL) {
        close_mark = strstr(open_mark + 2, "])");
        type = MSVG_NODE_STADIUM;
    }
    /* 2. Circle: ((Label)) */
    else if ((open_mark = strstr(token, "((")) != NULL) {
        close_mark = strstr(open_mark + 2, "))");
        type = MSVG_NODE_CIRCLE;
    }
    /* 3. Subroutine: [[Label]] */
    else if ((open_mark = strstr(token, "[[") ) != NULL) {
        close_mark = strstr(open_mark + 2, "]]");
        type = MSVG_NODE_SUBROUTINE;
    }
    /* 4. Cylinder: [(Label)] */
    else if ((open_mark = strstr(token, "[(")) != NULL) {
        close_mark = strstr(open_mark + 2, ")]");
        type = MSVG_NODE_CYLINDER;
    }
    /* 5. Diamond: {Label} */
    else if ((open_mark = strchr(token, '{')) != NULL) {
        close_mark = strchr(open_mark + 1, '}');
        type = MSVG_NODE_DIAMOND;
    }
    /* 6. Rounded Rect: (Label) */
    else if ((open_mark = strchr(token, '(')) != NULL) {
        close_mark = strchr(open_mark + 1, ')');
        type = MSVG_NODE_ROUND_RECT;
    }
    /* 7. Rectangle: [Label] */
    else if ((open_mark = strchr(token, '[')) != NULL) {
        close_mark = strchr(open_mark + 1, ']');
        type = MSVG_NODE_RECT;
    }

    if (open_mark) {
        size_t id_len = (size_t)(open_mark - token);
        if (id_len >= sizeof(id)) id_len = sizeof(id) - 1;
        memcpy(id, token, id_len);
        id[id_len] = '\0';
        msvg_trim(id);

        size_t open_skip = 1;
        if (type == MSVG_NODE_STADIUM || type == MSVG_NODE_CIRCLE ||
            type == MSVG_NODE_SUBROUTINE || type == MSVG_NODE_CYLINDER) {
            open_skip = 2;
        }

        if (close_mark) {
            size_t lab_len = (size_t)(close_mark - (open_mark + open_skip));
            if (lab_len >= sizeof(label)) lab_len = sizeof(label) - 1;
            memcpy(label, open_mark + open_skip, lab_len);
            label[lab_len] = '\0';
            msvg_trim(label);
        }
    } else {
        msvg_str_copy(id, sizeof(id), token);
        msvg_trim(id);
        msvg_str_copy(label, sizeof(label), id);
    }

    if (!id[0]) return false;
    int idx = msvg_find_or_add_node(g, id);
    if (idx >= 0) {
        if (label[0]) msvg_str_copy(g->nodes[idx].label, sizeof(g->nodes[idx].label), label);
        if (open_mark) g->nodes[idx].type = type;
        if (type == MSVG_NODE_DIAMOND) { g->nodes[idx].w = 90; g->nodes[idx].h = 60; }
        else if (type == MSVG_NODE_CIRCLE) { g->nodes[idx].w = 70; g->nodes[idx].h = 70; }
        else if (type == MSVG_NODE_STADIUM) { g->nodes[idx].w = 130; g->nodes[idx].h = 44; }
        *out_idx = idx;
        return true;
    }
    return false;
}

static char *mermaid_flowchart_to_svg(const char *mermaid_src) {
    MsvgGraph g;
    memset(&g, 0, sizeof(g));
    g.dir = MSVG_DIR_TD;

    char *src_copy = strdup(mermaid_src);
    if (!src_copy) return NULL;

    bool first_line = true;
    char *cursor = src_copy;

    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n' && *cursor != ';') cursor++;
        size_t line_len = (size_t)(cursor - line_start);
        if (*cursor) cursor++;

        char line_buf[512];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        char *comment = strstr(line_buf, "%%");
        if (comment) *comment = '\0';
        msvg_trim(line_buf);
        if (!line_buf[0]) continue;

        if (first_line) {
            if (strncmp(line_buf, "graph TD", 8) == 0 || strncmp(line_buf, "graph TB", 8) == 0 ||
                strncmp(line_buf, "flowchart TD", 12) == 0 || strncmp(line_buf, "flowchart TB", 12) == 0) {
                g.dir = MSVG_DIR_TD;
            } else if (strncmp(line_buf, "graph LR", 8) == 0 || strncmp(line_buf, "flowchart LR", 12) == 0) {
                g.dir = MSVG_DIR_LR;
            } else if (strncmp(line_buf, "graph RL", 8) == 0 || strncmp(line_buf, "flowchart RL", 12) == 0) {
                g.dir = MSVG_DIR_RL;
            }
            first_line = false;
        } else {
            char *arrow_pos = strstr(line_buf, "-->");
            char *line_pos  = strstr(line_buf, "---");
            if (arrow_pos || line_pos) {
                char *sep = arrow_pos ? arrow_pos : line_pos;
                MsvgEdgeType etype = arrow_pos ? MSVG_EDGE_ARROW : MSVG_EDGE_LINE;

                char left[128];
                size_t left_len = (size_t)(sep - line_buf);
                if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
                memcpy(left, line_buf, left_len);
                left[left_len] = '\0';
                msvg_trim(left);

                char *right = sep + 3;
                char edge_label[128] = {0};
                if (arrow_pos && right[0] == '|') {
                    char *end_label = strchr(right + 1, '|');
                    if (end_label) {
                        size_t l_len = (size_t)(end_label - (right + 1));
                        if (l_len >= sizeof(edge_label)) l_len = sizeof(edge_label) - 1;
                        memcpy(edge_label, right + 1, l_len);
                        edge_label[l_len] = '\0';
                        msvg_trim(edge_label);
                        right = end_label + 1;
                    }
                }
                msvg_trim(right);

                int src_idx = -1;
                msvg_parse_flow_node(&g, left, &src_idx);

                while (right && *right) {
                    char *next_arrow = strstr(right, "-->");
                    char *next_line  = strstr(right, "---");
                    char *next_sep = NULL;
                    if (next_arrow && next_line) next_sep = (next_arrow < next_line) ? next_arrow : next_line;
                    else if (next_arrow) next_sep = next_arrow;
                    else if (next_line) next_sep = next_line;

                    char target[128];
                    char next_label[128] = {0};
                    MsvgEdgeType next_etype = MSVG_EDGE_ARROW;

                    if (next_sep) {
                        next_etype = (next_sep == next_arrow) ? MSVG_EDGE_ARROW : MSVG_EDGE_LINE;
                        size_t tlen = (size_t)(next_sep - right);
                        if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
                        memcpy(target, right, tlen);
                        target[tlen] = '\0';
                        right = next_sep + 3;
                        if (next_etype == MSVG_EDGE_ARROW && right[0] == '|') {
                            char *el = strchr(right + 1, '|');
                            if (el) {
                                size_t ll = (size_t)(el - (right + 1));
                                if (ll >= sizeof(next_label)) ll = sizeof(next_label) - 1;
                                memcpy(next_label, right + 1, ll);
                                next_label[ll] = '\0';
                                msvg_trim(next_label);
                                right = el + 1;
                            }
                        }
                    } else {
                        msvg_str_copy(target, sizeof(target), right);
                        right = NULL;
                    }
                    msvg_trim(target);

                    int dst_idx = -1;
                    msvg_parse_flow_node(&g, target, &dst_idx);
                    if (src_idx >= 0 && dst_idx >= 0) {
                        msvg_add_flow_edge(&g, src_idx, dst_idx, etype, edge_label);
                    }
                    src_idx = dst_idx;
                    etype = next_etype;
                    msvg_str_copy(edge_label, sizeof(edge_label), next_label);
                    if (right) msvg_trim(right);
                }
            } else {
                int idx;
                msvg_parse_flow_node(&g, line_buf, &idx);
            }
        }
    }
    free(src_copy);

    if (g.node_count == 0) return NULL;

    /* BFS rank computation */
    int in_degrees[MSVG_MAX_NODES] = {0};
    for (int i = 0; i < g.edge_count; i++) in_degrees[g.edges[i].dst_idx]++;

    int queue[MSVG_MAX_NODES];
    int head = 0, tail = 0;
    for (int i = 0; i < g.node_count; i++) {
        if (in_degrees[i] == 0) { g.nodes[i].depth = 0; queue[tail++] = i; }
    }
    if (tail == 0) { g.nodes[0].depth = 0; queue[tail++] = 0; }

    int max_depth = 0;
    while (head < tail) {
        int u = queue[head++];
        if (g.nodes[u].depth > MSVG_MAX_BFS_DEPTH) break;
        if (g.nodes[u].depth > max_depth) max_depth = g.nodes[u].depth;
        for (int i = 0; i < g.edge_count; i++) {
            if (g.edges[i].src_idx == u) {
                int v = g.edges[i].dst_idx;
                if (g.nodes[v].depth == -1 || g.nodes[v].depth < g.nodes[u].depth + 1) {
                    g.nodes[v].depth = g.nodes[u].depth + 1;
                    if (tail < MSVG_MAX_NODES) queue[tail++] = v;
                }
            }
        }
    }
    for (int i = 0; i < g.node_count; i++) {
        if (g.nodes[i].depth == -1) g.nodes[i].depth = max_depth + 1;
    }

    int depth_counts[MSVG_MAX_BFS_DEPTH + 2];
    memset(depth_counts, 0, sizeof(depth_counts));
    for (int i = 0; i < g.node_count; i++) {
        int d = g.nodes[i].depth;
        if (d >= 0 && d <= MSVG_MAX_BFS_DEPTH + 1) {
            g.nodes[i].rank_index = depth_counts[d]++;
        }
    }

    float pad = 40.0f, h_sp = 190.0f, v_sp = 130.0f;
    float min_x = 1e6f, min_y = 1e6f, max_x = -1e6f, max_y = -1e6f;
    for (int i = 0; i < g.node_count; i++) {
        int d = g.nodes[i].depth, r = g.nodes[i].rank_index;
        int cnt = depth_counts[d];
        float cx, cy;
        if (g.dir == MSVG_DIR_TD) {
            cx = (r - (cnt - 1) / 2.0f) * h_sp;
            cy = d * v_sp;
        } else if (g.dir == MSVG_DIR_LR) {
            cx = d * h_sp;
            cy = (r - (cnt - 1) / 2.0f) * v_sp;
        } else {
            cx = -d * h_sp;
            cy = (r - (cnt - 1) / 2.0f) * v_sp;
        }
        g.nodes[i].x = cx - g.nodes[i].w / 2.0f;
        g.nodes[i].y = cy - g.nodes[i].h / 2.0f;
        if (g.nodes[i].x < min_x) min_x = g.nodes[i].x;
        if (g.nodes[i].y < min_y) min_y = g.nodes[i].y;
        if (g.nodes[i].x + g.nodes[i].w > max_x) max_x = g.nodes[i].x + g.nodes[i].w;
        if (g.nodes[i].y + g.nodes[i].h > max_y) max_y = g.nodes[i].y + g.nodes[i].h;
    }
    min_x -= pad; min_y -= pad; max_x += pad; max_y += pad;

    size_t out_sz = 2048;
    size_t out_len = 0;
    char *out = malloc(out_sz);
    if (!out) return NULL;
    out[0] = '\0';

    msvg_svg_append_fmt(&out, &out_sz, &out_len,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"mermaid-diagram\" "
        "viewBox=\"%.0f %.0f %.0f %.0f\">\n"
        "<defs>\n"
        "  <marker id=\"msvg-arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
        "markerWidth=\"8\" markerHeight=\"8\" orient=\"auto\">\n"
        "    <path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#5f6368\"/>\n"
        "  </marker>\n"
        "</defs>\n", min_x, min_y, max_x - min_x, max_y - min_y);

    /* Edges */
    for (int i = 0; i < g.edge_count; i++) {
        MsvgNode *s = &g.nodes[g.edges[i].src_idx];
        MsvgNode *d = &g.nodes[g.edges[i].dst_idx];
        float sx = s->x + s->w / 2.0f, sy = s->y + s->h / 2.0f;
        float tx = d->x + d->w / 2.0f, ty = d->y + d->h / 2.0f;
        if (g.dir == MSVG_DIR_TD) { sy += s->h / 2.0f; ty -= d->h / 2.0f; }
        else if (g.dir == MSVG_DIR_LR) { sx += s->w / 2.0f; tx -= d->w / 2.0f; }
        else { sx -= s->w / 2.0f; tx += d->w / 2.0f; }

        const char *marker = (g.edges[i].type == MSVG_EDGE_ARROW) ? " marker-end=\"url(#msvg-arrow)\"" : "";
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" "
            "stroke=\"#5f6368\" stroke-width=\"2\"%s/>\n", sx, sy, tx, ty, marker);

        if (g.edges[i].label[0]) {
            float mx = (sx + tx) / 2.0f, my = (sy + ty) / 2.0f;
            size_t lbl_w = strlen(g.edges[i].label) * 7 + 14;
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<rect x=\"%.0f\" y=\"%.0f\" width=\"%zu\" height=\"20\" fill=\"white\" rx=\"4\"/>\n"
                "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"11\" "
                "text-anchor=\"middle\" dominant-baseline=\"middle\">%s</text>\n",
                mx - (float)lbl_w / 2.0f, my - 10.0f, lbl_w, mx, my, g.edges[i].label);
        }
    }

    /* Nodes */
    for (int i = 0; i < g.node_count; i++) {
        MsvgNode *n = &g.nodes[i];
        float cx = n->x + n->w / 2.0f, cy = n->y + n->h / 2.0f;

        if (n->type == MSVG_NODE_DIAMOND) {
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<polygon points=\"%.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f\" "
                "fill=\"#fef7e0\" stroke=\"#f9ab00\" stroke-width=\"2\"/>\n",
                cx, n->y, n->x + n->w, cy, cx, n->y + n->h, n->x, cy);
        } else if (n->type == MSVG_NODE_CIRCLE) {
            float r = n->w / 2.0f;
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<circle cx=\"%.0f\" cy=\"%.0f\" r=\"%.0f\" fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"2\"/>\n",
                cx, cy, r);
        } else if (n->type == MSVG_NODE_STADIUM) {
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" "
                "fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"2\" rx=\"22\"/>\n",
                n->x, n->y, n->w, n->h);
        } else if (n->type == MSVG_NODE_SUBROUTINE) {
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"2\" rx=\"4\"/>\n"
                "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#4285f4\" stroke-width=\"1.5\"/>\n"
                "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#4285f4\" stroke-width=\"1.5\"/>\n",
                n->x, n->y, n->w, n->h, n->x + 10.0f, n->y, n->x + 10.0f, n->y + n->h, n->x + n->w - 10.0f, n->y, n->x + n->w - 10.0f, n->y + n->h);
        } else {
            int rx = (n->type == MSVG_NODE_ROUND_RECT) ? 16 : 4;
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" "
                "fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"2\" rx=\"%d\"/>\n",
                n->x, n->y, n->w, n->h, rx);
        }

        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"13\" "
            "text-anchor=\"middle\" dominant-baseline=\"middle\">%s</text>\n",
            cx, cy, n->label);
    }

    msvg_svg_append(&out, &out_sz, &out_len, "</svg>\n");
    return out;
}

/* -------------------------------------------------------------------------
 * 2. SEQUENCE DIAGRAM ENGINE
 * ------------------------------------------------------------------------- */
typedef struct {
    char name[64];
    char label[64];
    float x;
} MsvgParticipant;

typedef struct {
    int from_idx;
    int to_idx;
    bool is_dashed;
    bool is_arrow;
    char text[128];
} MsvgSeqMessage;

static char *mermaid_sequence_to_svg(const char *mermaid_src) {
    MsvgParticipant parts[MSVG_MAX_PARTICIPANTS];
    int part_count = 0;
    MsvgSeqMessage msgs[MSVG_MAX_MESSAGES];
    int msg_count = 0;

    char *src_copy = strdup(mermaid_src);
    if (!src_copy) return NULL;

    char *cursor = src_copy;
    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n' && *cursor != ';') cursor++;
        size_t line_len = (size_t)(cursor - line_start);
        if (*cursor) cursor++;

        char line_buf[512];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        char *comment = strstr(line_buf, "%%");
        if (comment) *comment = '\0';
        msvg_trim(line_buf);
        if (!line_buf[0] || strncmp(line_buf, "sequenceDiagram", 15) == 0) continue;

        /* participant <Name> [as <Alias>] */
        if (strncmp(line_buf, "participant ", 12) == 0) {
            char *p = line_buf + 12;
            msvg_trim(p);
            char name[64] = {0}, label[64] = {0};
            char *as_pos = strstr(p, " as ");
            if (as_pos) {
                size_t nlen = (size_t)(as_pos - p);
                if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                memcpy(name, p, nlen);
                name[nlen] = '\0';
                msvg_trim(name);
                msvg_str_copy(label, sizeof(label), as_pos + 4);
                msvg_trim(label);
            } else {
                msvg_str_copy(name, sizeof(name), p);
                msvg_str_copy(label, sizeof(label), p);
            }
            if (name[0] && part_count < MSVG_MAX_PARTICIPANTS) {
                msvg_str_copy(parts[part_count].name, sizeof(parts[part_count].name), name);
                msvg_str_copy(parts[part_count].label, sizeof(parts[part_count].label), label);
                part_count++;
            }
            continue;
        }

        /* Message arrow: A->>B: text, A-->>B: text, A->B: text, A-->B: text */
        char *arrow = NULL;
        bool dashed = false;
        bool has_arrow = true;
        size_t arrow_len = 0;

        if ((arrow = strstr(line_buf, "-->>")) != NULL) { dashed = true; has_arrow = true; arrow_len = 4; }
        else if ((arrow = strstr(line_buf, "->>")) != NULL) { dashed = false; has_arrow = true; arrow_len = 3; }
        else if ((arrow = strstr(line_buf, "-->")) != NULL) { dashed = true; has_arrow = false; arrow_len = 3; }
        else if ((arrow = strstr(line_buf, "->")) != NULL) { dashed = false; has_arrow = false; arrow_len = 2; }

        if (arrow && msg_count < MSVG_MAX_MESSAGES) {
            char from_name[64] = {0}, to_name[64] = {0}, msg_text[128] = {0};
            size_t flen = (size_t)(arrow - line_buf);
            if (flen >= sizeof(from_name)) flen = sizeof(from_name) - 1;
            memcpy(from_name, line_buf, flen);
            from_name[flen] = '\0';
            msvg_trim(from_name);

            char *after_arrow = arrow + arrow_len;
            char *colon = strchr(after_arrow, ':');
            if (colon) {
                size_t tlen = (size_t)(colon - after_arrow);
                if (tlen >= sizeof(to_name)) tlen = sizeof(to_name) - 1;
                memcpy(to_name, after_arrow, tlen);
                to_name[tlen] = '\0';
                msvg_trim(to_name);
                msvg_str_copy(msg_text, sizeof(msg_text), colon + 1);
                msvg_trim(msg_text);
            } else {
                msvg_str_copy(to_name, sizeof(to_name), after_arrow);
                msvg_trim(to_name);
            }

            /* Find or add participants */
            int f_idx = -1, t_idx = -1;
            for (int i = 0; i < part_count; i++) {
                if (strcmp(parts[i].name, from_name) == 0) f_idx = i;
                if (strcmp(parts[i].name, to_name) == 0) t_idx = i;
            }
            if (f_idx == -1 && part_count < MSVG_MAX_PARTICIPANTS) {
                f_idx = part_count;
                msvg_str_copy(parts[part_count].name, sizeof(parts[part_count].name), from_name);
                msvg_str_copy(parts[part_count].label, sizeof(parts[part_count].label), from_name);
                part_count++;
            }
            if (t_idx == -1 && part_count < MSVG_MAX_PARTICIPANTS) {
                t_idx = part_count;
                msvg_str_copy(parts[part_count].name, sizeof(parts[part_count].name), to_name);
                msvg_str_copy(parts[part_count].label, sizeof(parts[part_count].label), to_name);
                part_count++;
            }

            if (f_idx >= 0 && t_idx >= 0) {
                msgs[msg_count].from_idx = f_idx;
                msgs[msg_count].to_idx = t_idx;
                msgs[msg_count].is_dashed = dashed;
                msgs[msg_count].is_arrow = has_arrow;
                msvg_str_copy(msgs[msg_count].text, sizeof(msgs[msg_count].text), msg_text);
                msg_count++;
            }
        }
    }
    free(src_copy);

    if (part_count == 0) return NULL;

    /* Compute layout */
    float col_w = 170.0f;
    float start_x = 90.0f;
    for (int i = 0; i < part_count; i++) {
        parts[i].x = start_x + i * col_w;
    }
    float svg_w = start_x * 2.0f + (part_count - 1) * col_w;
    float top_box_y = 20.0f, box_h = 36.0f, box_w = 110.0f;
    float start_msg_y = 85.0f, msg_spacing = 46.0f;
    float bottom_box_y = start_msg_y + msg_count * msg_spacing + 20.0f;
    float svg_h = bottom_box_y + box_h + 20.0f;

    size_t out_sz = 4096;
    size_t out_len = 0;
    char *out = malloc(out_sz);
    if (!out) return NULL;
    out[0] = '\0';

    msvg_svg_append_fmt(&out, &out_sz, &out_len,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"mermaid-diagram\" viewBox=\"0 0 %.0f %.0f\">\n"
        "<defs>\n"
        "  <marker id=\"seq-arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"8\" markerHeight=\"8\" orient=\"auto\">\n"
        "    <path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#1a73e8\"/>\n"
        "  </marker>\n"
        "</defs>\n", svg_w, svg_h);

    /* Lifelines */
    for (int i = 0; i < part_count; i++) {
        float x = parts[i].x;
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#dadce0\" stroke-width=\"1.5\" stroke-dasharray=\"4,4\"/>\n",
            x, top_box_y + box_h, x, bottom_box_y);
    }

    /* Messages */
    for (int i = 0; i < msg_count; i++) {
        float sx = parts[msgs[i].from_idx].x;
        float tx = parts[msgs[i].to_idx].x;
        float y = start_msg_y + i * msg_spacing;

        const char *dash = msgs[i].is_dashed ? " stroke-dasharray=\"4,4\"" : "";
        const char *marker = msgs[i].is_arrow ? " marker-end=\"url(#seq-arrow)\"" : "";

        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#1a73e8\" stroke-width=\"2\"%s%s/>\n",
            sx, y, tx, y, dash, marker);

        if (msgs[i].text[0]) {
            float mx = (sx + tx) / 2.0f;
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#202124\" "
                "text-anchor=\"middle\">%s</text>\n",
                mx, y - 8.0f, msgs[i].text);
        }
    }

    /* Top and Bottom Actor Boxes */
    for (int i = 0; i < part_count; i++) {
        float x = parts[i].x - box_w / 2.0f;
        /* Top box */
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"1.5\" rx=\"6\"/>\n"
            "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"13\" font-weight=\"600\" fill=\"#1a73e8\" "
            "text-anchor=\"middle\" dominant-baseline=\"middle\">%s</text>\n",
            x, top_box_y, box_w, box_h, parts[i].x, top_box_y + box_h / 2.0f, parts[i].label);
        /* Bottom box */
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"1.5\" rx=\"6\"/>\n"
            "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"13\" font-weight=\"600\" fill=\"#1a73e8\" "
            "text-anchor=\"middle\" dominant-baseline=\"middle\">%s</text>\n",
            x, bottom_box_y, box_w, box_h, parts[i].x, bottom_box_y + box_h / 2.0f, parts[i].label);
    }

    msvg_svg_append(&out, &out_sz, &out_len, "</svg>\n");
    return out;
}

/* -------------------------------------------------------------------------
 * 3. CLASS DIAGRAM ENGINE
 * ------------------------------------------------------------------------- */
typedef struct {
    char name[64];
    char members[MSVG_MAX_CLASS_MEMBERS][64];
    int member_count;
    float x, y, w, h;
} MsvgClass;

typedef struct {
    int src_idx;
    int dst_idx;
    char type[16];
} MsvgClassRel;

static char *mermaid_class_to_svg(const char *mermaid_src) {
    MsvgClass classes[MSVG_MAX_CLASSES];
    int class_count = 0;
    MsvgClassRel rels[MSVG_MAX_EDGES];
    int rel_count = 0;

    char *src_copy = strdup(mermaid_src);
    if (!src_copy) return NULL;

    int cur_class_idx = -1;
    char *cursor = src_copy;

    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n' && *cursor != ';') cursor++;
        size_t line_len = (size_t)(cursor - line_start);
        if (*cursor) cursor++;

        char line_buf[512];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        char *comment = strstr(line_buf, "%%");
        if (comment) *comment = '\0';
        msvg_trim(line_buf);
        if (!line_buf[0] || strncmp(line_buf, "classDiagram", 12) == 0) continue;

        if (cur_class_idx >= 0 && strcmp(line_buf, "}") == 0) {
            cur_class_idx = -1;
            continue;
        }

        /* Class declaration: class Name { or class Name */
        if (strncmp(line_buf, "class ", 6) == 0) {
            char *p = line_buf + 6;
            msvg_trim(p);
            char cname[64] = {0};
            char *open_brace = strchr(p, '{');
            if (open_brace) {
                size_t nlen = (size_t)(open_brace - p);
                if (nlen >= sizeof(cname)) nlen = sizeof(cname) - 1;
                memcpy(cname, p, nlen);
                cname[nlen] = '\0';
                msvg_trim(cname);
            } else {
                msvg_str_copy(cname, sizeof(cname), p);
                msvg_trim(cname);
            }
            int idx = -1;
            for (int i = 0; i < class_count; i++) {
                if (strcmp(classes[i].name, cname) == 0) { idx = i; break; }
            }
            if (idx == -1 && class_count < MSVG_MAX_CLASSES) {
                idx = class_count++;
                msvg_str_copy(classes[idx].name, sizeof(classes[idx].name), cname);
                classes[idx].member_count = 0;
            }
            if (open_brace) cur_class_idx = idx;
            continue;
        }

        /* Members inside class */
        if (cur_class_idx >= 0) {
            if (classes[cur_class_idx].member_count < MSVG_MAX_CLASS_MEMBERS) {
                int mc = classes[cur_class_idx].member_count++;
                msvg_str_copy(classes[cur_class_idx].members[mc], sizeof(classes[cur_class_idx].members[mc]), line_buf);
            }
            continue;
        }

        /* Class relationship: A --> B or A <|-- B */
        char *rel_sep = NULL;
        if ((rel_sep = strstr(line_buf, "<|--")) != NULL || (rel_sep = strstr(line_buf, "-->")) != NULL ||
            (rel_sep = strstr(line_buf, "--*")) != NULL  || (rel_sep = strstr(line_buf, "--o")) != NULL  ||
            (rel_sep = strstr(line_buf, "---")) != NULL) {
            char left[64] = {0}, right[64] = {0};
            size_t llen = (size_t)(rel_sep - line_buf);
            if (llen >= sizeof(left)) llen = sizeof(left) - 1;
            memcpy(left, line_buf, llen);
            left[llen] = '\0';
            msvg_trim(left);

            size_t rlen = 3;
            if (strncmp(rel_sep, "<|--", 4) == 0) rlen = 4;
            msvg_str_copy(right, sizeof(right), rel_sep + rlen);
            msvg_trim(right);

            int s_idx = -1, d_idx = -1;
            for (int i = 0; i < class_count; i++) {
                if (strcmp(classes[i].name, left) == 0) s_idx = i;
                if (strcmp(classes[i].name, right) == 0) d_idx = i;
            }
            if (s_idx == -1 && class_count < MSVG_MAX_CLASSES) {
                s_idx = class_count++;
                msvg_str_copy(classes[s_idx].name, sizeof(classes[s_idx].name), left);
                classes[s_idx].member_count = 0;
            }
            if (d_idx == -1 && class_count < MSVG_MAX_CLASSES) {
                d_idx = class_count++;
                msvg_str_copy(classes[d_idx].name, sizeof(classes[d_idx].name), right);
                classes[d_idx].member_count = 0;
            }
            if (s_idx >= 0 && d_idx >= 0 && rel_count < MSVG_MAX_EDGES) {
                rels[rel_count].src_idx = s_idx;
                rels[rel_count].dst_idx = d_idx;
                msvg_str_copy(rels[rel_count].type, sizeof(rels[rel_count].type), rel_sep);
                rel_count++;
            }
        }
    }
    free(src_copy);

    if (class_count == 0) return NULL;

    /* Compute layout for classes */
    float cur_x = 40.0f;
    float max_h = 0.0f;
    for (int i = 0; i < class_count; i++) {
        classes[i].w = 180.0f;
        classes[i].h = 36.0f + classes[i].member_count * 20.0f + 10.0f;
        if (classes[i].member_count == 0) classes[i].h = 50.0f;
        classes[i].x = cur_x;
        classes[i].y = 40.0f;
        cur_x += classes[i].w + 90.0f;
        if (classes[i].h > max_h) max_h = classes[i].h;
    }
    float svg_w = cur_x - 10.0f;
    float svg_h = max_h + 100.0f;

    size_t out_sz = 4096;
    size_t out_len = 0;
    char *out = malloc(out_sz);
    if (!out) return NULL;
    out[0] = '\0';

    msvg_svg_append_fmt(&out, &out_sz, &out_len,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"mermaid-diagram\" viewBox=\"0 0 %.0f %.0f\">\n"
        "<defs>\n"
        "  <marker id=\"class-arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"8\" markerHeight=\"8\" orient=\"auto\">\n"
        "    <path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#5f6368\"/>\n"
        "  </marker>\n"
        "</defs>\n", svg_w, svg_h);

    /* Relationships */
    for (int i = 0; i < rel_count; i++) {
        MsvgClass *s = &classes[rels[i].src_idx];
        MsvgClass *d = &classes[rels[i].dst_idx];
        float sx = s->x + s->w, sy = s->y + s->h / 2.0f;
        float tx = d->x, ty = d->y + d->h / 2.0f;
        if (s->x > d->x) {
            sx = s->x;
            tx = d->x + d->w;
        }
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<line x1=\"%.0f\" y1=\"%.0f\" x2=\"%.0f\" y2=\"%.0f\" stroke=\"#5f6368\" stroke-width=\"1.5\" marker-end=\"url(#class-arrow)\"/>\n",
            sx, sy, tx, ty);
    }

    /* Class Boxes */
    for (int i = 0; i < class_count; i++) {
        MsvgClass *c = &classes[i];
        /* Outer box */
        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"%.0f\" fill=\"white\" stroke=\"#4285f4\" stroke-width=\"1.5\" rx=\"4\"/>\n"
            "<rect x=\"%.0f\" y=\"%.0f\" width=\"%.0f\" height=\"32\" fill=\"#e8f0fe\" stroke=\"#4285f4\" stroke-width=\"1.5\" rx=\"4\"/>\n"
            "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"13\" font-weight=\"bold\" fill=\"#1a73e8\" "
            "text-anchor=\"middle\" dominant-baseline=\"middle\">%s</text>\n",
            c->x, c->y, c->w, c->h, c->x, c->y, c->w, c->x + c->w / 2.0f, c->y + 16.0f, c->name);

        float mem_y = c->y + 48.0f;
        for (int m = 0; m < c->member_count; m++) {
            msvg_svg_append_fmt(&out, &out_sz, &out_len,
                "<text x=\"%.0f\" y=\"%.0f\" font-family=\"monospace\" font-size=\"11\" fill=\"#3c4043\">%s</text>\n",
                c->x + 10.0f, mem_y, c->members[m]);
            mem_y += 20.0f;
        }
    }

    msvg_svg_append(&out, &out_sz, &out_len, "</svg>\n");
    return out;
}

/* -------------------------------------------------------------------------
 * 4. PIE CHART ENGINE
 * ------------------------------------------------------------------------- */
typedef struct {
    char label[64];
    float value;
} MsvgPieSlice;

static char *mermaid_pie_to_svg(const char *mermaid_src) {
    char title[128] = "Pie Chart";
    MsvgPieSlice slices[MSVG_MAX_PIE_SLICES];
    int slice_count = 0;
    float total_val = 0.0f;

    char *src_copy = strdup(mermaid_src);
    if (!src_copy) return NULL;

    char *cursor = src_copy;
    while (*cursor) {
        char *line_start = cursor;
        while (*cursor && *cursor != '\n' && *cursor != ';') cursor++;
        size_t line_len = (size_t)(cursor - line_start);
        if (*cursor) cursor++;

        char line_buf[512];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        char *comment = strstr(line_buf, "%%");
        if (comment) *comment = '\0';
        msvg_trim(line_buf);
        if (!line_buf[0]) continue;

        /* pie title <Title> or pie */
        if (strncmp(line_buf, "pie", 3) == 0) {
            char *t = strstr(line_buf, "title ");
            if (t) {
                msvg_str_copy(title, sizeof(title), t + 6);
                msvg_trim(title);
            }
            continue;
        }

        /* "Label" : value or Label : value */
        char *colon = strchr(line_buf, ':');
        if (colon && slice_count < MSVG_MAX_PIE_SLICES) {
            char lab[64] = {0};
            size_t llen = (size_t)(colon - line_buf);
            if (llen >= sizeof(lab)) llen = sizeof(lab) - 1;
            memcpy(lab, line_buf, llen);
            lab[llen] = '\0';
            msvg_trim(lab);

            /* Strip quotes if present */
            if (lab[0] == '"' && lab[strlen(lab) - 1] == '"') {
                lab[strlen(lab) - 1] = '\0';
                memmove(lab, lab + 1, strlen(lab));
            }

            float val = (float)atof(colon + 1);
            if (val > 0.0f) {
                msvg_str_copy(slices[slice_count].label, sizeof(slices[slice_count].label), lab);
                slices[slice_count].value = val;
                total_val += val;
                slice_count++;
            }
        }
    }
    free(src_copy);

    if (slice_count == 0 || total_val <= 0.0f) return NULL;

    const char *colors[] = {
        "#4285f4", "#ea4335", "#fbbc04", "#34a853", "#aa66cc", "#ff7f0e", "#17becf", "#e377c2"
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);

    float cx = 150.0f, cy = 160.0f, r = 100.0f;
    float cur_angle = -1.57079632679f; /* Start at 12 o'clock */

    size_t out_sz = 4096;
    size_t out_len = 0;
    char *out = malloc(out_sz);
    if (!out) return NULL;
    out[0] = '\0';

    msvg_svg_append_fmt(&out, &out_sz, &out_len,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"mermaid-diagram\" viewBox=\"0 0 540 320\">\n"
        "<text x=\"270\" y=\"32\" font-family=\"sans-serif\" font-size=\"16\" font-weight=\"bold\" "
        "text-anchor=\"middle\" fill=\"#202124\">%s</text>\n", title);

    /* Pie Slices */
    for (int i = 0; i < slice_count; i++) {
        float slice_angle = (slices[i].value / total_val) * 6.28318530718f;
        float next_angle = cur_angle + slice_angle;

        float x1 = cx + r * msvg_cos(cur_angle);
        float y1 = cy + r * msvg_sin(cur_angle);
        float x2 = cx + r * msvg_cos(next_angle);
        float y2 = cy + r * msvg_sin(next_angle);

        int large_arc = (slice_angle > 3.14159265f) ? 1 : 0;
        const char *col = colors[i % num_colors];

        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<path d=\"M %.1f %.1f L %.1f %.1f A %.1f %.1f 0 %d 1 %.1f %.1f Z\" fill=\"%s\" stroke=\"white\" stroke-width=\"1.5\"/>\n",
            cx, cy, x1, y1, r, r, large_arc, x2, y2, col);

        cur_angle = next_angle;
    }

    /* Legend */
    float leg_x = 290.0f;
    float leg_y = 85.0f;
    for (int i = 0; i < slice_count; i++) {
        const char *col = colors[i % num_colors];
        float pct = (slices[i].value / total_val) * 100.0f;

        msvg_svg_append_fmt(&out, &out_sz, &out_len,
            "<rect x=\"%.0f\" y=\"%.0f\" width=\"14\" height=\"14\" fill=\"%s\" rx=\"2\"/>\n"
            "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#3c4043\">%s: %.1f%%</text>\n",
            leg_x, leg_y + i * 26.0f, col, leg_x + 24.0f, leg_y + i * 26.0f + 11.0f, slices[i].label, pct);
    }

    msvg_svg_append(&out, &out_sz, &out_len, "</svg>\n");
    return out;
}

/* -------------------------------------------------------------------------
 * MAIN DISPATCHER
 * ------------------------------------------------------------------------- */
static char *mermaid_to_svg(const char *mermaid_src) {
    if (!mermaid_src) return NULL;

    const char *p = mermaid_src;
    while (*p && isspace((unsigned char)*p)) p++;

    if (strncmp(p, "graph ", 6) == 0 || strncmp(p, "flowchart ", 10) == 0) {
        return mermaid_flowchart_to_svg(mermaid_src);
    }
    if (strncmp(p, "sequenceDiagram", 15) == 0) {
        return mermaid_sequence_to_svg(mermaid_src);
    }
    if (strncmp(p, "classDiagram", 12) == 0) {
        return mermaid_class_to_svg(mermaid_src);
    }
    if (strncmp(p, "pie", 3) == 0) {
        return mermaid_pie_to_svg(mermaid_src);
    }

    return NULL;
}
