"""Prepare publisher HTML for the small plain-text reader without discarding structure."""
import re
from html import escape
from html.parser import HTMLParser
from urllib.parse import urlsplit, urljoin, unquote

VOID = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr'}


class Node:
    def __init__(self, tag='', attrs=None, children=None):
        self.tag, self.attrs, self.children = tag, dict(attrs or []), children or []

    def walk(self):
        yield self
        for child in self.children:
            if isinstance(child, Node):
                yield from child.walk()

    def text(self):
        return ''.join(child.text() if isinstance(child, Node) else child for child in self.children)

    def html(self):
        content = ''.join(child.html() if isinstance(child, Node) else escape(child) for child in self.children)
        if not self.tag:
            return content
        attrs = ''.join(f' {key}="{escape(value or "", quote=True)}"' for key, value in self.attrs.items())
        return f'<{self.tag}{attrs}>' + ('' if self.tag in VOID else content + f'</{self.tag}>')


class Tree(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node()
        self.stack = [self.root]

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs)
        self.stack[-1].children.append(node)
        if tag not in VOID:
            self.stack.append(node)

    def handle_endtag(self, tag):
        for i in range(len(self.stack)-1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        self.stack[-1].children.append(data)


def _normalized(text):
    return re.sub(r'\s+', ' ', text).strip(' “ ” ‘ ’\n')


def prepare_html(markup, base_url='', collect_links=False, rich=False):
    tree = Tree()
    tree.feed(markup or '')
    tree.close()
    nodes = list(tree.root.walk())
    pulls = [node for node in nodes if 'pullquote' in node.attrs.get('class', '')]
    def outside(node):
        if node in pulls:
            return ''
        return ''.join(outside(child) if isinstance(child, Node) else child for child in node.children)
    ordinary = _normalized(outside(tree.root))
    for node in pulls:
        text = _normalized(node.text())
        if text and text in ordinary:
            node.children = []
        elif text:
            node.attrs['data-pullquote'] = '1'

    targets = {node.attrs['id']: node for node in nodes
               if re.match(r'^(?:fn(?:\d|[-_:])|footnote[-_])', node.attrs.get('id', ''), re.I)}
    numbers = {}
    for node in nodes:
        if node.tag != 'a':
            continue
        fragment = unquote(urlsplit(node.attrs.get('href', '')).fragment)
        if fragment in targets:
            number = numbers.setdefault(fragment, len(numbers)+1)
            node.tag, node.attrs, node.children = 'span', {}, [f'[{number}]']
    for ident, number in numbers.items():
        note = targets[ident]
        note.attrs['data-footnote-number'] = str(number)
        backs = [node for node in note.walk() if node.tag == 'a' and
                 re.match(r'^(?:fnr|fnref|footnote-ref)', unquote(urlsplit(node.attrs.get('href', '')).fragment), re.I)]
        for node in backs:
            node.tag, node.attrs, node.children = 'span', {}, [f'[Back to {number}]']
        if not backs:
            note.children.extend([' ', Node('span', children=[f'[Back to {number}]'])])

    for node in tree.root.walk():
        if node.tag == 'a':
            href = node.attrs.get('href', '')
            if not href or href.startswith('#'):
                continue
            url = urljoin(base_url, href)
            parsed = urlsplit(url)
            if parsed.scheme in ('http', 'https') and parsed.hostname:
                if collect_links:
                    if node.text().strip():
                        node.attrs['data-link-url'] = parsed.geturl()
                    continue
                display = url.split('://', 1)[1]
                if parsed.path in ('', '/') and not parsed.query and not parsed.fragment:
                    display = display.rstrip('/')
                if node.text().strip() and _normalized(node.text()).rstrip('/') not in (url.rstrip('/'), display):
                    node.children.append(f' [{display}]')
        elif node.tag == 'figcaption' and not node.text().strip():
            node.tag = 'span'
        elif node.tag in ('iframe', 'audio', 'video'):
            label = {'iframe': 'Embedded media', 'audio': 'Audio', 'video': 'Video'}[node.tag]
            node.children.insert(0, f'[{label}: {node.attrs.get("title", "").strip() or "not shown"}]')
            node.tag = 'div'
        elif node.tag == 'figure' and ('embed' in node.attrs.get('class', '')) and not node.text().strip():
            node.children = ['[Embedded media not included in feed]']
    if rich:
        # Pad cells before rendering, keeping their inline markup and links.
        for table in [n for n in nodes if n.tag == 'table']:
            rows = []
            occupied = {}
            for row_index, row in enumerate(n for n in table.walk() if n.tag == 'tr'):
                cells, column = [], 0
                for cell in (n for n in row.children if isinstance(n, Node) and n.tag in ('td', 'th')):
                    while occupied.get(column, 0) > row_index:
                        cells.append(Node('span')); column += 1
                    try:
                        colspan = max(1, min(64, int(cell.attrs.get('colspan', '1'))))
                        rowspan = max(1, min(256, int(cell.attrs.get('rowspan', '1'))))
                    except ValueError:
                        colspan = rowspan = 1
                    cell.tag = 'strong' if cell.tag == 'th' else 'span'
                    cells.append(cell)
                    for c in range(column, column+colspan): occupied[c] = row_index+rowspan
                    cells.extend(Node('span') for _ in range(colspan-1)); column += colspan
                while column < max(occupied, default=-1)+1:
                    cells.append(Node('span')); column += 1
                rows.append(cells)
            if rows:
                columns = max(map(len, rows))
                # Use the client's encoded glyph count, including symbol fallbacks.
                from protocol import _client_text
                def tidy(node):
                    node.children = [Node('span', children=[' ']) if isinstance(ch, Node) and ch.tag == 'br' else ch for ch in node.children]
                    for i, ch in enumerate(node.children):
                        if isinstance(ch, Node): tidy(ch)
                        else: node.children[i] = re.sub(r'\s+', ' ', ch)
                def cell_width(cell): return len(_client_text(cell.text(), 65535, True))
                for row in rows:
                    for cell in row: tidy(cell)
                widths = [max((cell_width(row[c]) if c < len(row) else 0) for row in rows) for c in range(columns)]
                children = []
                for row in rows:
                    for c in range(columns):
                        cell = row[c] if c < len(row) else Node('span')
                        children.append(cell)
                        if c < columns-1: children.append(' ' * max(0, widths[c]-cell_width(cell)) + ' | ')
                    children.append('\n')
                captions = [Node('p', children=['Table: ', *n.children]) for n in table.children if isinstance(n, Node) and n.tag == 'caption']
                table.tag = 'div'; table.attrs = {}; table.children = [*captions, Node('pre', {'data-table': '1'}, children)]
    return tree.root.html()
