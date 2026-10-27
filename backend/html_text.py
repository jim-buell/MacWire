"""Render HTML as readable plain text for the classic Mac client.

Source whitespace flows like HTML; paragraphs, explicit line breaks and
preformatted blocks supply the hard breaks. Block quotes remain visible in plain text; optional link and style ranges
preserve richer presentation without placing markup in the visible body.
"""
import re
from html.parser import HTMLParser
from html_structure import prepare_html
from quote_text import CODE_MARKER, normalize_quotes


BLOCKS = {'p', 'div', 'li', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
          'ul', 'ol', 'section', 'article', 'tr', 'figure', 'figcaption'}
CHROME = {'script', 'style', 'head', 'nav', 'header', 'footer', 'aside', 'form', 'noscript'}
LINK_MARKER = r'\x1d(\d+)([SE])\x1c'
STYLE_MARKER = r'\x1b(\d+)([SE])\x1a'
MARKERS = '(?:' + LINK_MARKER + '|' + STYLE_MARKER + '|' + CODE_MARKER + ')'
STYLE_FLAGS = {'b': 1, 'strong': 1, 'i': 2, 'em': 2, 'code': 4, 'kbd': 4, 'samp': 4, 'pre': 8, 'sup': 64, 'sub': 128, 's': 256, 'strike': 256, 'del': 256}


class HTMLTextRenderer(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.text = ''
        self.quotes = []
        self.pre_depth = 0
        self.lists = []
        self.quote_starts = []
        self.cells = 0

    def _break(self, count=2):
        visible = re.sub(r'\x1e\d+\x1f', '', re.sub(MARKERS, '', self.text))
        if re.search(r'(?:^|[\n“‘]) *(?:•|\d+\.|\[\d+\]:|Caption:|#{1,3}) $', visible):
            return
        self.text = self.text.rstrip(' \t')
        if not visible or visible.endswith(('“', '‘')):
            return
        existing = len(visible) - len(visible.rstrip('\n'))
        self.text += '\n' * max(0, count - existing)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag in ('code', 'pre', 'kbd', 'samp'):
            self.text += '\x19S\x18'
        if attrs.get('data-pullquote'):
            self._break()
            self.text += 'Pull quote:\n'
        if tag in ('ul', 'ol'):
            self.lists.append([tag, int(attrs.get('start', '1')) if attrs.get('start', '1').isdigit() else 1])
        if attrs.get('data-footnote-number'):
            self._break()
            self.text += '[' + attrs['data-footnote-number'] + ']: '
            return
        if tag in ('blockquote', 'q'):
            if tag == 'blockquote': self._break()
            pair = ('“', '”') if len(self.quotes) % 2 == 0 else ('‘', '’')
            self.quotes.append(pair)
            self.quote_starts.append(len(self.text))
            self.text += pair[0]
        elif tag == 'li':
            self._break()
            self.text += '  ' * max(0, len(self.lists)-1)
            if self.lists and self.lists[-1][0] == 'ol':
                self.text += str(self.lists[-1][1]) + '. '
                self.lists[-1][1] += 1
            else:
                self.text += '• '
        elif tag == 'tr':
            self._break()
            self.cells = 0
        elif tag in ('td', 'th'):
            if self.cells:
                self.text = self.text.rstrip() + ' | '
            self.cells += 1
        elif tag == 'figcaption':
            self._break()
            self.text += 'Caption: '
        elif tag == 'hr':
            self._break()
            self.text += '---'
            self._break()
        elif tag in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6'):
            self._break()
            self.text += '#' * min(3, int(tag[1])) + ' '
        elif tag == 'br':
            self._break(1)
        elif tag == 'pre':
            self._break()
            self.pre_depth += 1
        elif tag in BLOCKS:
            self._break()

    def handle_endtag(self, tag):
        if tag in ('code', 'pre', 'kbd', 'samp'):
            self.text += '\x19E\x18'
        if tag in ('ul', 'ol') and self.lists:
            self.lists.pop()
        if tag in ('blockquote', 'q') and self.quotes:
            pair = self.quotes.pop()
            start = self.quote_starts.pop()
            self.text = self.text.rstrip(' \t\r\n')
            if normalize_quotes(self.text[start+1:], MARKERS)[1]:
                self.text = self.text[:start] + self.text[start+1:]
            else:
                self.text += pair[1]
            if tag == 'blockquote': self._break()
        elif tag == 'pre':
            self.pre_depth = max(0, self.pre_depth - 1)
            self._break()
        elif tag in BLOCKS:
            self._break()

    def handle_data(self, data):
        if not self.pre_depth:
            data = re.sub(r'\s+', ' ', data)
            visible = re.sub(MARKERS, '', self.text)
            if not visible or visible.endswith(('\n', ' ', '“', '‘')):
                data = data.lstrip(' ')
        self.text += data

    def get_text(self):
        normalized, _ = normalize_quotes(self.text, MARKERS)
        return re.sub(MARKERS, '', normalized).strip()


def html_to_text(markup, base_url=''):
    parser = HTMLTextRenderer()
    parser.feed(prepare_html(markup, base_url))
    parser.close()
    return parser.get_text()


class HTMLImageTextRenderer(HTMLTextRenderer):
    """Keep image anchors while normalizing surrounding HTML text."""
    def __init__(self, images=None, base_url='', collect_images=False, links=None, styles=None):
        super().__init__()
        self.images = images if images is not None else []
        self.base_url = base_url
        self.collect_images = collect_images
        self.seen = set()
        self.links = links
        self.link_urls = []
        self.link_stack = []
        self.styles = styles
        self.style_flags = []
        self.style_stack = []

    def handle_starttag(self, tag, attrs):
        if tag == 'a' and self.links is not None:
            url = dict(attrs).get('data-link-url')
            index = len(self.link_urls) if url else None
            self.link_stack.append(index)
            if index is not None:
                self.link_urls.append(url)
                self.text += f'\x1d{index}S\x1c'
        if tag != 'img':
            super().handle_starttag(tag, attrs)
            if self.styles is not None:
                attributes = dict(attrs)
                flags = STYLE_FLAGS.get(tag, 0)
                if attributes.get('data-table'): flags |= 16
                if attributes.get('data-pullquote'): flags |= 32 | 2
                index = len(self.style_flags) if flags else None
                if tag not in ('br', 'hr', 'input', 'source', 'wbr'):
                    self.style_stack.append((tag, index))
                if index is not None:
                    self.style_flags.append(flags)
                    self.text += f'\x1b{index}S\x1a'
            return
        from urllib.parse import urljoin
        attrs = dict(attrs)
        source = attrs.get('data-src') or attrs.get('data-original') or attrs.get('src')
        if not source:
            return
        url = urljoin(self.base_url, source)
        if not url.startswith(('http://', 'https://')):
            return
        try:
            width, height = int(attrs.get('width', 0)), int(attrs.get('height', 0))
        except (TypeError, ValueError):
            width = height = 0
        if width == 1 or height == 1:
            return
        index = next((i for i, image in enumerate(self.images) if image['url'] == url), None)
        if index is None and self.collect_images and len(self.images) < 5:
            index = len(self.images)
            self.images.append({'url': url, 'width': width, 'height': height, 'format': 'unknown'})
        if index is None:
            self._break()
            self.text += '[Photo omitted: image limit]'
            self._break()
            return
        if index in self.seen:
            return
        self.seen.add(index)
        self._break()
        self.text += f'\x1e{index}\x1f'
        self._break()

    def handle_endtag(self, tag):
        if self.styles is not None:
            for i in range(len(self.style_stack)-1, -1, -1):
                if self.style_stack[i][0] == tag:
                    for _, index in reversed(self.style_stack[i:]):
                        if index is not None: self.text += f'\x1b{index}E\x1a'
                    del self.style_stack[i:]
                    break
        if tag == 'a' and self.links is not None and self.link_stack:
            index = self.link_stack.pop()
            if index is not None:
                self.text += f'\x1d{index}E\x1c'
        super().handle_endtag(tag)

    def get_text(self):
        raw, _ = normalize_quotes(self.text.strip(" \t\r\n"), MARKERS + r"|\x1e\d+\x1f")
        pattern = r'\x1e(\d+)\x1f'
        def clean(value):
            value = re.sub(MARKERS, '', re.sub(pattern, '', value))
            return value if self.styles is not None else re.sub(r'\n{3,}', '\n\n', value)
        text = clean(raw).strip("\r\n") if self.styles is not None else clean(raw).strip()
        for match in re.finditer(pattern, raw):
            prefix = clean(raw[:match.start()]).lstrip("\r\n" if self.styles is not None else None)
            self.images[int(match[1])]['offset'] = min(len(prefix), len(text))
        if self.links is not None:
            self.links.clear()
            starts = {}
            for match in re.finditer(LINK_MARKER, raw):
                index = int(match[1])
                offset = min(len(clean(raw[:match.start()]).lstrip("\r\n" if self.styles is not None else None)), len(text))
                if match[2] == 'S':
                    starts[index] = offset
                elif index in starts:
                    start = starts.pop(index)
                    while start < offset and text[start].isspace(): start += 1
                    while offset > start and text[offset-1].isspace(): offset -= 1
                    if offset > start:
                        self.links.append({'start': start, 'length': offset-start, 'url': self.link_urls[index]})
        if self.styles is not None:
            self.styles.clear()
            starts = {}
            for match in re.finditer(STYLE_MARKER, raw):
                index = int(match[1])
                offset = min(len(clean(raw[:match.start()]).lstrip("\r\n" if self.styles is not None else None)), len(text))
                if match[2] == 'S': starts[index] = offset
                elif index in starts:
                    start = starts.pop(index)
                    if offset > start: self.styles.append({'start': start, 'length': offset-start, 'flags': self.style_flags[index]})
        return text


def html_to_text_with_images(markup, images, base_url='', links=None, styles=None):
    parser = HTMLImageTextRenderer(images, base_url, links=links, styles=styles)
    parser.feed(prepare_html(markup, base_url, collect_links=links is not None, rich=styles is not None))
    parser.close()
    return parser.get_text()
