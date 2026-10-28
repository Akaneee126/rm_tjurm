# -*- coding: utf-8 -*-
"""Second-pass repair: the cp936 round-trip sometimes also ate the newline at
the end of a comment line, merging the following code line into the comment.

Each entry: line_number -> (replacements, split_marker or None)
  replacements : list of (old, new) applied to the comment part
  split_marker : text where the original newline should be re-inserted

Line numbers are 1-based and refer to the file's current state.
"""
import io
import sys

NMS = {
    45:  ([('?', '例')], 'if (width_ratio > height_ratio) {'),
    54:  ([('?', '形')], 'static cv::Rect nms_get_rect(yolofpRaw* yolo_raw) {'),
    105: ([('?', '标')], 'int x1 = std::max(box1.x, box2.x);'),
    107: ([('?', '标')], 'int x2 = std::min(box1.x + box1.width, box2.x + box2.width);'),
    124: ([('?', '数')], 'yolofpRaw* yolo_raw = (yolofpRaw*)(output_buffer + i * yolo_size);'),
    142: ([('?', '滤')], 'bool flag_pose = true;'),
    153: ([('?', '值')], 'YoloRect detection_rect;'),
    169: ([('?', '数')], 'yolov5Raw* yolo_raw = (yolov5Raw*)(output_buffer + i * yolo_size);'),
    186: ([('?', '值')], 'YoloRect detection_rect;'),
    200: ([('颜色?', '颜色数'), ('类别?', '类别数')],
          'yolofpRaw* yolo_raw = (yolofpRaw*)(output_buffer + i * yolo_size);'),
    205: ([('?', '选')], 'int color_index = -1;'),
    218: ([('?', '选')], 'int class_index = -1;'),
    231: ([('?', '滤')], 'bool flag_pose = true;'),
    242: ([('?', '值')], 'YoloRect detection_rect;'),
    271: ([('?', '历')], 'if (list.size() <= 1) return;'),
    273: ([('?', '始')], 'YoloRect focus_rect = list[0];'),
    275: ([('?', '去')], 'retained_list = std::vector<YoloRect>();'),
}

V5C36 = {
    29:  ([('?', '值')], 'static void nms_member_init('),
    59:  ([('?', '例')], 'if (width_ratio > height_ratio) {'),
    98:  ([('?', '（')], None),
    99:  ([('?', '（')], None),
    102: ([('?', '标')], 'int x1 = std::max(box1.x, box2.x);'),
    104: ([('?', '标')], 'int x2 = std::min(box1.x + box1.width, box2.x + box2.width);'),
    110: ([('?', '积')], None),
    121: ([('?', '选')], 'if (output_buffer[i * yolo_size + 4] < confidence_threshold) {'),
    125: ([('?', '数')], 'yoloArmorRaw_V5C36* yolo_raw = (yoloArmorRaw_V5C36*)(output_buffer + i * yolo_size);'),
    139: ([('?', '值')], 'YoloRect detection_rect;'),
    151: ([('?', '别')], 'for (auto& detection_rect_list: detection_class_list) {'),
    172: ([('?', '历')], 'for (auto& detection_rect_list: detection_class_list) {'),
    177: ([('?', '始')], 'YoloRect focus_rect = detection_rect_list[0];'),
    179: ([('?', '去')], 'retained_rect_list = std::vector<YoloRect>();'),
    234: ([('?', '入')], 'if (result_rect_list.empty()) {'),
    259: ([('?', '束')], 'avaliable_rect = false;'),
    299: ([('?', '值')], 'std::vector<YoloRect> result_rect_list;'),
}

DASHBOARD = {
    32: ([('?', '色')], 'init_pair(1, COLOR_WHITE, COLOR_RED);'),
}

OSC = {
    86: ([('?', '值')], 'for(int j = 0; j < canvas_w; j++) {'),
    92: ([('?', '轴')], 'for (int i = 0; i < h - 1; i++) mvprintw(i, 5, "|");'),
    101: ([('?', '形')], 'for (size_t i = 0; i < msg_name.size(); i++) {'),
}

JOBS = [
    ('OpenRM-main/OpenRM-main/src/infer/nms.cpp', NMS),
    ('OpenRM-main/OpenRM-main/src/infer/nmsV5C36.cpp', V5C36),
    ('OpenRM-main/OpenRM-main/src/uniterm/dashboard.cpp', DASHBOARD),
    ('OpenRM-main/OpenRM-main/src/uniterm/oscilloscope.cpp', OSC),
]

for path, table in JOBS:
    text = io.open(path, encoding='utf-8').read()
    lines = text.split('\n')
    changed = 0
    for ln, (repls, marker) in table.items():
        i = ln - 1
        line = lines[i]
        if marker is None:
            new = line
            for old, new_c in repls:
                new = new.replace(old, new_c, 1)
            lines[i] = new
            changed += 1
            continue
        pos = line.find(marker)
        if pos < 0:
            print('!! marker not found %s:%d %r' % (path, ln, marker))
            continue
        comment = line[:pos].rstrip()
        code = line[pos:]
        for old, new_c in repls:
            comment = comment.replace(old, new_c, 1)
        lines[i] = comment + '\n' + code
        changed += 1
    io.open(path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
    print('%-58s fixed %d line(s)' % (path, changed))
