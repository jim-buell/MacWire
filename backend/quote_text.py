"""Alternate paired quotation marks without treating apostrophes as quotes."""
import re

CODE_MARKER = r'\x19([SE])\x18'


def normalize_quotes(raw, markers):
    """Return equal-length text and whether one quote pair encloses it all.

    Internal link/style/code markers are invisible. Code is preserved literally,
    as are unmatched quotes and inch marks immediately following digits.
    """
    marker = re.compile(markers)
    visible = marker.sub('', raw)
    first = len(visible)-len(visible.lstrip())
    last = len(visible.rstrip())-1
    tokens = re.compile(markers + r'|["“”‘’]')
    result = list(raw)
    stack = []
    code_depth = 0
    fully_quoted = False
    raw_end = visible_position = 0
    for token in tokens.finditer(raw):
        character = token.group()
        visible_position += token.start()-raw_end
        raw_end = token.end()
        if len(character) != 1:
            if re.fullmatch(CODE_MARKER, character):
                code_depth += 1 if character[1] == 'S' else -1
                code_depth = max(0, code_depth)
            continue
        position = visible_position
        visible_position += 1
        if code_depth:
            continue
        before = visible[position-1] if position else ''
        after = visible[position+1] if position+1 < len(visible) else ''
        # Smart apostrophes in contractions/possessives aren't delimiters.
        if character == '’' and before.isalnum() and after.isalnum():
            continue
        if character == '"' and before.isdigit() and (not stack or stack[-1][3] != '"'):
            continue
        kind = 'double' if character in '"“”' else 'single'
        opening = character in '“‘'
        if character == '"':
            opening = bool(after and not after.isspace() and
                           (not before or before.isspace() or before in '([{—–,:;“‘"'))
            if not stack and not opening:
                continue
        if opening:
            if stack:
                pair = ('‘', '’') if stack[-1][4][0] == '“' else ('“', '”')
            else:
                pair = ('‘', '’') if kind == 'single' else ('“', '”')
            stack.append((kind, token.start(), position, character, pair))
            result[token.start()] = pair[0]
        elif stack and stack[-1][0] == kind:
            _, start, start_visible, original, pair = stack.pop()
            result[token.start()] = pair[1]
            if start_visible == first and position == last:
                fully_quoted = True
    # Don't rewrite opening marks that never found a corresponding close.
    for _, start, _, original, _ in stack:
        result[start] = original
    return ''.join(result), fully_quoted
