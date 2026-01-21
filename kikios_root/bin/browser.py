# browser.py - Kivi Web Browser for KikiOS
# Supports file://, http://, https:// protocols
# Renders HTML with TTF fonts

import vibe

# Constants
WIN_W = 640
WIN_H = 480
MARGIN = 10
SCROLLBAR_W = 12
CONTENT_W = WIN_W - MARGIN * 2 - SCROLLBAR_W
ADDRESS_BAR_H = 24
CONTENT_Y = ADDRESS_BAR_H + 2
CONTENT_H = WIN_H - CONTENT_Y

# Colors
WHITE = 0x00FFFFFF
BLACK = 0x00000000
GRAY = 0x00888888
LIGHT_GRAY = 0x00CCCCCC
LINK_COLOR = 0x000000FF
BLOCKQUOTE_BG = 0x00EEEEEE

# Font sizes for HTML elements
FONT_SIZES = {
    'h1': 28, 'h2': 24, 'h3': 20, 'h4': 18, 'h5': 16, 'h6': 14,
    'default': 16
}

# HTML entity map
HTML_ENTITIES = {
    'amp': '&', 'lt': '<', 'gt': '>', 'quot': '"', 'apos': "'",
    'nbsp': ' ', 'copy': '(c)', 'reg': '(R)', 'trade': '(TM)',
    'mdash': '-', 'ndash': '-', 'bull': '*', 'hellip': '...',
    'laquo': '<<', 'raquo': '>>', 'ldquo': '"', 'rdquo': '"',
    'lsquo': "'", 'rsquo': "'", 'pound': 'GBP', 'euro': 'EUR',
    'yen': 'JPY', 'cent': 'c', 'times': 'x', 'divide': '/',
    'plusmn': '+/-', 'deg': 'deg', 'larr': '<-', 'rarr': '->',
    'uarr': '^', 'darr': 'v', 'middot': '.', 'sect': 'S',
    'para': 'P', 'dagger': '+', 'permil': 'o/oo', 'prime': "'",
    'infin': 'inf', 'ne': '!=', 'le': '<=', 'ge': '>=',
    'asymp': '~=', 'equiv': '===', 'alpha': 'a', 'beta': 'b',
    'gamma': 'g', 'delta': 'd', 'pi': 'pi', 'sigma': 's', 'omega': 'w'
}

# ============================================================================
# Welcome Page
# ============================================================================

WELCOME_PAGE = '''<!DOCTYPE html>
<html>
<head><title>Welcome to Kivi</title></head>
<body>
<h1>Welcome to Kivi Browser</h1>
<p>A modern web browser for KikiOS with HTTPS support.</p>
<hr>
<h2>Try these links:</h2>
<ul>
<li><a href="https://example.com">example.com</a> - A simple test page (HTTPS)</li>
<li><a href="http://info.cern.ch">info.cern.ch</a> - The first website ever</li>
<li><a href="http://motherfuckingwebsite.com">motherfuckingwebsite.com</a> - Clean HTML sites work best</li>
<li><a href="https://www.google.com">google.com</a> - Try HTTPS sites!</li>
</ul>
<hr>
<h2>Features:</h2>
<ul>
<li>HTTP and HTTPS support with TLS 1.2</li>
<li>HTML rendering with TTF fonts</li>
<li>Link navigation and history</li>
<li>Local file browsing (file://)</li>
</ul>
<hr>
<p>Tip: Click the address bar to type a URL, press Enter to navigate.</p>
<p>Use the back button or Page Up/Down to navigate.</p>
</body>
</html>'''

# ============================================================================
# URL Parser
# ============================================================================

def parse_url(url):
    """Parse URL into (scheme, host, port, path)"""
    if url.startswith('file://'):
        return ('file', None, None, url[7:])

    if url.startswith('https://'):
        scheme = 'https'
        url = url[8:]
        default_port = 443
    elif url.startswith('http://'):
        scheme = 'http'
        url = url[7:]
        default_port = 80
    else:
        scheme = 'http'
        default_port = 80

    slash_pos = url.find('/')
    if slash_pos == -1:
        host_port = url
        path = '/'
    else:
        host_port = url[:slash_pos]
        path = url[slash_pos:]

    colon_pos = host_port.find(':')
    if colon_pos == -1:
        host = host_port
        port = default_port
    else:
        host = host_port[:colon_pos]
        port = int(host_port[colon_pos + 1:])

    return (scheme, host, port, path)

def resolve_url(base_url, rel_url):
    """Resolve relative URL against base URL"""
    if rel_url.startswith('http://') or rel_url.startswith('https://') or rel_url.startswith('file://') or rel_url.startswith('about:'):
        return rel_url

    scheme, host, port, path = parse_url(base_url)

    if rel_url.startswith('/'):
        new_path = rel_url
    else:
        base_dir = path.rsplit('/', 1)[0] + '/'
        new_path = base_dir + rel_url

    if scheme == 'file':
        return 'file://' + new_path
    else:
        result = scheme + '://' + host
        if (scheme == 'http' and port != 80) or (scheme == 'https' and port != 443):
            result += ':' + str(port)
        return result + new_path

# ============================================================================
# HTTP Fetcher
# ============================================================================

def fetch_url(url):
    """Fetch content from URL, returns (status, body)"""
    # Handle special about: URLs
    if url == 'about:home' or url == 'about:blank':
        return (200, WELCOME_PAGE)

    scheme, host, port, path = parse_url(url)

    if scheme == 'file':
        return fetch_file(path)

    ip = vibe.dns_resolve(host)
    if ip == 0:
        return (0, "DNS lookup failed for " + str(host))

    if scheme == 'https':
        sock = vibe.tls_connect(ip, port, host)
        send_fn = vibe.tls_send
        recv_fn = vibe.tls_recv
        close_fn = vibe.tls_close
    else:
        sock = vibe.tcp_connect(ip, port)
        send_fn = vibe.tcp_send
        recv_fn = vibe.tcp_recv
        close_fn = vibe.tcp_close

    if sock < 0:
        return (0, "Connection failed to " + host)

    request = "GET " + path + " HTTP/1.0\r\n"
    request += "Host: " + host + "\r\n"
    request += "User-Agent: Kivi/1.0 (KikiOS)\r\n"
    request += "Connection: close\r\n"
    request += "\r\n"
    send_fn(sock, request)

    response = b''
    timeout = 0
    while timeout < 100:
        chunk = recv_fn(sock, 4096)
        if chunk is None or len(chunk) == 0:
            if len(response) > 0:
                break
            timeout += 1
            vibe.sleep_ms(50)
        else:
            response += chunk
            timeout = 0
        vibe.sched_yield()

    close_fn(sock)

    return parse_http_response(response)

def fetch_file(path):
    """Load content from local file"""
    f = vibe.open(path)
    if f is None:
        return (404, "File not found: " + path)

    size = vibe.file_size(f)
    data = vibe.read(f, size, 0)

    if data is None:
        return (500, "Failed to read file")

    content = ''
    for b in data:
        content += chr(b)
    return (200, content)

def parse_http_response(data):
    """Parse HTTP response into (status, body)"""
    sep = data.find(b'\r\n\r\n')
    if sep == -1:
        return (0, "Invalid HTTP response")

    header_bytes = data[:sep]
    body_bytes = data[sep + 4:]

    header_str = ''
    for b in header_bytes:
        header_str += chr(b)

    lines = header_str.split('\r\n')
    parts = lines[0].split(' ')
    status = int(parts[1]) if len(parts) >= 2 else 0

    # Check for redirect
    if status in (301, 302, 307, 308):
        for line in lines[1:]:
            if line.lower().startswith('location:'):
                return (status, line[9:].strip())

    body = ''
    for b in body_bytes:
        body += chr(b)
    return (status, body)

# ============================================================================
# HTML Parser
# ============================================================================

def decode_entities(text):
    """Decode HTML entities in text"""
    result = ''
    i = 0
    while i < len(text):
        if text[i] == '&':
            end = text.find(';', i)
            if end != -1 and end - i < 12:
                entity = text[i+1:end]
                if entity.startswith('#x'):
                    try:
                        result += chr(int(entity[2:], 16))
                    except:
                        result += text[i:end+1]
                elif entity.startswith('#'):
                    try:
                        result += chr(int(entity[1:]))
                    except:
                        result += text[i:end+1]
                elif entity in HTML_ENTITIES:
                    result += HTML_ENTITIES[entity]
                else:
                    result += text[i:end+1]
                i = end + 1
                continue
        result += text[i]
        i += 1
    return result

class Element:
    """Simple DOM element"""
    def __init__(self, tag, attrs=None):
        self.tag = tag.lower() if tag else ''
        self.attrs = attrs if attrs else {}
        self.children = []
        self.text = ''

def parse_attrs(attr_str):
    """Parse HTML attributes"""
    attrs = {}
    i = 0
    while i < len(attr_str):
        while i < len(attr_str) and attr_str[i] in ' \t\n':
            i += 1
        if i >= len(attr_str):
            break

        start = i
        while i < len(attr_str) and attr_str[i] not in '= \t\n':
            i += 1
        name = attr_str[start:i].lower()

        while i < len(attr_str) and attr_str[i] in ' \t\n=':
            i += 1

        if i < len(attr_str) and attr_str[i] in '"\'':
            quote = attr_str[i]
            i += 1
            start = i
            while i < len(attr_str) and attr_str[i] != quote:
                i += 1
            value = attr_str[start:i]
            i += 1
        else:
            start = i
            while i < len(attr_str) and attr_str[i] not in ' \t\n>':
                i += 1
            value = attr_str[start:i]

        if name:
            attrs[name] = value
    return attrs

def parse_html(html):
    """Parse HTML into element tree"""
    root = Element('root')
    stack = [root]
    void_tags = {'br', 'hr', 'img', 'input', 'meta', 'link'}
    i = 0

    while i < len(html):
        if html[i] == '<':
            end = html.find('>', i)
            if end == -1:
                break

            tag_content = html[i+1:end].strip()

            if tag_content.startswith('!'):
                i = end + 1
                continue

            if tag_content.startswith('/'):
                tag_name = tag_content[1:].strip().lower()
                while len(stack) > 1 and stack[-1].tag != tag_name:
                    stack.pop()
                if len(stack) > 1:
                    stack.pop()
                i = end + 1
                continue

            space = tag_content.find(' ')
            if space == -1:
                tag_name = tag_content.rstrip('/').lower()
                attrs = {}
            else:
                tag_name = tag_content[:space].lower()
                attrs = parse_attrs(tag_content[space:])

            elem = Element(tag_name, attrs)
            stack[-1].children.append(elem)

            if tag_name not in void_tags and not tag_content.endswith('/'):
                stack.append(elem)

            i = end + 1
        else:
            next_tag = html.find('<', i)
            if next_tag == -1:
                next_tag = len(html)

            text = html[i:next_tag]
            if text.strip():
                text = decode_entities(text)
                text = ' '.join(text.split())
                if stack[-1].text:
                    stack[-1].text += ' '
                stack[-1].text += text

            i = next_tag

    return root

# ============================================================================
# Layout Engine
# ============================================================================

class TextBlock:
    """A rendered text block"""
    def __init__(self, x, y, text, font_size, style, fg, href=None):
        self.x = x
        self.y = y
        self.text = text
        self.font_size = font_size
        self.style = style
        self.fg = fg
        self.href = href
        self.w = 0
        self.h = 0

def measure_text(text, font_size):
    """Measure text width using TTF metrics"""
    width = 0
    prev_cp = 0
    for ch in text:
        cp = ord(ch)
        width += vibe.ttf_get_advance(cp, font_size)
        if prev_cp:
            width += vibe.ttf_get_kerning(prev_cp, cp, font_size)
        prev_cp = cp
    return width

def find_element(root, tag):
    """Find first element with given tag"""
    if root.tag == tag:
        return root
    for child in root.children:
        found = find_element(child, tag)
        if found:
            return found
    return None

def layout_element(elem, blocks, links, y, indent, font_size, style, href):
    """Layout a single element and its children, returns new y position"""
    tag = elem.tag

    # Update font size based on tag
    if tag in FONT_SIZES:
        font_size = FONT_SIZES[tag]

    # Update style
    if tag in ('b', 'strong'):
        style = style | vibe.TTF_BOLD
    elif tag in ('i', 'em'):
        style = style | vibe.TTF_ITALIC

    # Track link href
    if tag == 'a':
        href = elem.attrs.get('href', '')

    ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
    line_height = ascent - descent + line_gap

    # Block elements
    if tag in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6'):
        y += line_height // 2
        y = layout_text(get_all_text(elem), blocks, links, y, indent, font_size, style | vibe.TTF_BOLD, BLACK, href)
        y += line_height // 4
        return y

    if tag == 'p':
        y += line_height // 4
        y = layout_inline(elem, blocks, links, y, indent, font_size, style, href)
        y += line_height // 4
        return y

    if tag == 'br':
        return y + line_height

    if tag == 'hr':
        y += 4
        blocks.append(TextBlock(MARGIN + indent, y, None, 0, 0, GRAY))
        blocks[-1].w = CONTENT_W - indent
        blocks[-1].h = 1
        blocks[-1].text = '__HR__'
        return y + 8

    if tag in ('ul', 'ol'):
        y += 4
        list_num = 1
        for child in elem.children:
            if child.tag == 'li':
                # Draw bullet/number first
                bullet = '* ' if tag == 'ul' else str(list_num) + '.'
                ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
                line_height = ascent - descent + line_gap
                block = TextBlock(MARGIN + indent + 20, y, bullet, font_size, style, BLACK, None)
                block.w = measure_text(bullet, font_size)
                block.h = line_height
                blocks.append(block)
                # Layout li children (preserves links)
                for li_child in child.children:
                    y = layout_element(li_child, blocks, links, y, indent + 40, font_size, style, None)
                # Also handle direct text in li
                if child.text:
                    y = layout_text(child.text, blocks, links, y, indent + 40, font_size, style, BLACK, None)
                if y == block.y:  # Nothing was added, just advance
                    y += line_height
                list_num += 1
        return y + 4

    if tag == 'blockquote':
        y += 8
        # Add marker for background
        blocks.append(TextBlock(MARGIN + indent, y, '__BQ_START__', 0, 0, GRAY))
        blocks[-1].w = CONTENT_W - indent
        start_idx = len(blocks) - 1

        y = layout_text(get_all_text(elem), blocks, links, y, indent + 20, font_size, style | vibe.TTF_ITALIC, BLACK, href)

        # Mark end
        blocks[start_idx].h = y - blocks[start_idx].y + 8
        return y + 8

    if tag == 'pre' or tag == 'code':
        y += 4
        text = get_all_text(elem)
        for line in text.split('\n'):
            if line:
                y = layout_text(line, blocks, links, y, indent, 14, 0, BLACK, None)
        return y + 4

    if tag == 'table':
        return layout_table(elem, blocks, links, y, indent, font_size, style)

    # Default: layout text and children
    if elem.text:
        y = layout_text(elem.text, blocks, links, y, indent, font_size, style,
                        LINK_COLOR if href else BLACK, href)

    for child in elem.children:
        y = layout_element(child, blocks, links, y, indent, font_size, style, href)

    return y

def get_all_text(elem):
    """Get all text content recursively"""
    result = elem.text
    for child in elem.children:
        result += get_all_text(child)
    return result

def layout_inline(elem, blocks, links, y, indent, font_size, style, href):
    """Layout inline content with mixed styles"""
    x = MARGIN + indent
    max_x = MARGIN + CONTENT_W

    ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
    line_height = ascent - descent + line_gap

    def emit_word(word, cur_style, cur_href, cur_fg):
        nonlocal x, y
        if not word:
            return

        word_w = measure_text(word, font_size)

        if x + word_w > max_x and x > MARGIN + indent:
            x = MARGIN + indent
            y += line_height

        block = TextBlock(x, y, word, font_size, cur_style, cur_fg, cur_href)
        block.w = word_w
        block.h = line_height
        blocks.append(block)

        if cur_href:
            links.append((x, y, word_w, line_height, cur_href))

        x += word_w + measure_text(' ', font_size)

    def process(el, cur_style, cur_href):
        s = cur_style
        if el.tag in ('b', 'strong'):
            s = s | vibe.TTF_BOLD
        elif el.tag in ('i', 'em'):
            s = s | vibe.TTF_ITALIC

        h = cur_href
        fg = LINK_COLOR if h else BLACK
        if el.tag == 'a':
            h = el.attrs.get('href', '')
            fg = LINK_COLOR

        if el.text:
            words = el.text.split()
            for word in words:
                emit_word(word, s, h, fg)

        for child in el.children:
            process(child, s, h)

    process(elem, style, href)
    return y + line_height

def layout_text(text, blocks, links, y, indent, font_size, style, fg, href):
    """Layout a block of text with word wrapping"""
    if not text:
        return y

    ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
    line_height = ascent - descent + line_gap

    words = text.split()
    x = MARGIN + indent
    max_x = MARGIN + CONTENT_W
    line_words = []

    for word in words:
        word_w = measure_text(word, font_size)
        test_w = measure_text(' '.join(line_words + [word]), font_size) if line_words else word_w

        if x + test_w > max_x and line_words:
            # Emit current line
            line_text = ' '.join(line_words)
            block = TextBlock(x, y, line_text, font_size, style, fg, href)
            block.w = measure_text(line_text, font_size)
            block.h = line_height
            blocks.append(block)
            if href:
                links.append((x, y, block.w, line_height, href))
            y += line_height
            line_words = [word]
            x = MARGIN + indent
        else:
            line_words.append(word)

    if line_words:
        line_text = ' '.join(line_words)
        block = TextBlock(x, y, line_text, font_size, style, fg, href)
        block.w = measure_text(line_text, font_size)
        block.h = line_height
        blocks.append(block)
        if href:
            links.append((x, y, block.w, line_height, href))
        y += line_height

    return y

def layout_table(table_elem, blocks, links, y, indent, font_size, style):
    """Layout a table with columns"""
    rows = []
    for child in table_elem.children:
        if child.tag in ('thead', 'tbody'):
            for row in child.children:
                if row.tag == 'tr':
                    cells = []
                    for cell in row.children:
                        if cell.tag in ('td', 'th'):
                            cells.append((cell.tag, get_all_text(cell)))
                    if cells:
                        rows.append(cells)
        elif child.tag == 'tr':
            cells = []
            for cell in child.children:
                if cell.tag in ('td', 'th'):
                    cells.append((cell.tag, get_all_text(cell)))
            if cells:
                rows.append(cells)

    if not rows:
        return y

    num_cols = max(len(row) for row in rows)
    col_width = (CONTENT_W - indent - MARGIN) // num_cols

    ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
    row_height = ascent - descent + line_gap + 8

    y += 4

    for row in rows:
        cell_x = MARGIN + indent
        for i, (cell_type, text) in enumerate(row):
            cell_style = style | vibe.TTF_BOLD if cell_type == 'th' else style

            # Draw cell border marker
            blocks.append(TextBlock(cell_x, y, '__CELL__', 0, 0, BLACK))
            blocks[-1].w = col_width
            blocks[-1].h = row_height

            # Draw cell text (truncate if needed)
            max_chars = col_width // 8
            display_text = text[:max_chars] if len(text) > max_chars else text

            block = TextBlock(cell_x + 4, y + 4, display_text, font_size, cell_style, BLACK)
            block.w = col_width - 8
            block.h = row_height - 8
            blocks.append(block)

            cell_x += col_width

        y += row_height

    return y + 4

# ============================================================================
# Renderer
# ============================================================================

def render(wid, blocks, scroll_y, content_y, content_h, win_w, win_h):
    """Render all visible blocks"""
    vibe.window_fill_rect(wid, 0, content_y, win_w, content_h, WHITE)

    for block in blocks:
        by = block.y - scroll_y + content_y

        if by + block.h < content_y or by > win_h:
            continue

        if block.text == '__HR__':
            vibe.window_draw_hline(wid, block.x, by, block.w, block.fg)
        elif block.text == '__BQ_START__':
            vibe.window_fill_rect(wid, block.x, by, block.w, block.h, BLOCKQUOTE_BG)
            vibe.window_fill_rect(wid, block.x, by, 3, block.h, GRAY)
        elif block.text == '__CELL__':
            vibe.window_draw_rect(wid, block.x, by, block.w, block.h, BLACK)
        elif block.text:
            render_text(wid, block.x, by, block.text, block.font_size, block.style, block.fg)
            if block.href:
                ascent, descent, line_gap = vibe.ttf_get_metrics(block.font_size)
                vibe.window_draw_hline(wid, block.x, by + ascent + 2, block.w, LINK_COLOR)

def render_text(wid, x, y, text, font_size, style, fg):
    """Render text with TTF font"""
    if not vibe.ttf_is_ready():
        vibe.window_draw_string(wid, x, y, text, fg, WHITE)
        return

    ascent, descent, line_gap = vibe.ttf_get_metrics(font_size)
    baseline_y = y + ascent

    for ch in text:
        cp = ord(ch)
        glyph = vibe.ttf_get_glyph(cp, font_size, style)
        if glyph and glyph['bitmap']:
            vibe.window_draw_glyph(
                wid,
                x + glyph['xoff'], baseline_y + glyph['yoff'],
                glyph['bitmap'], glyph['width'], glyph['height'],
                fg, WHITE
            )
        x += vibe.ttf_get_advance(cp, font_size)

# ============================================================================
# Browser Class
# ============================================================================

class Browser:
    def __init__(self, kiosk_mode=False):
        self.wid = -1
        self.url = ''
        self.history = []
        self.forward_history = []
        self.blocks = []
        self.links = []
        self.scroll_y = 0
        self.max_scroll = 0
        self.content_height = 0
        self.address_text = ''
        self.editing_url = False
        self.cursor_pos = 0
        self.kiosk_mode = kiosk_mode
        # Window dimensions (updated on resize)
        self.win_w = WIN_W
        self.win_h = WIN_H
        # Adjust content area based on mode
        # Kiosk mode has small nav buttons at top (20px)
        self.content_y = 20 if kiosk_mode else ADDRESS_BAR_H + 2
        self.content_h = self.win_h - 20 if kiosk_mode else self.win_h - self.content_y
        self.content_w = self.win_w - MARGIN * 2 - SCROLLBAR_W
        # Scrollbar dragging state
        self.scrollbar_dragging = False
        self.scrollbar_drag_start_y = 0
        self.scrollbar_drag_start_scroll = 0
        # Current HTML for relayout on resize
        self.current_html = ''

    def layout_document(self, root):
        """Layout the entire document using current window dimensions"""
        global CONTENT_W
        CONTENT_W = self.content_w
        blocks = []
        links = []
        y = MARGIN

        body = find_element(root, 'body')
        if not body:
            body = root

        y = layout_element(body, blocks, links, y, 0, 16, 0, None)

        # Calculate actual content height from blocks
        max_bottom = y
        for block in blocks:
            block_bottom = block.y + block.h
            if block_bottom > max_bottom:
                max_bottom = block_bottom

        content_height = max_bottom + MARGIN

        return blocks, links, content_height * 2

    def handle_resize(self, new_w, new_h):
        """Handle window resize"""
        self.win_w = new_w
        self.win_h = new_h
        self.content_h = self.win_h - 20 if self.kiosk_mode else self.win_h - self.content_y
        self.content_w = self.win_w - MARGIN * 2 - SCROLLBAR_W

        # Re-layout if we have content
        if self.current_html:
            root = parse_html(self.current_html)
            self.blocks, self.links, self.content_height = self.layout_document(root)
            self.max_scroll = max(0, self.content_height - self.content_h)
            # Clamp scroll position
            if self.scroll_y > self.max_scroll:
                self.scroll_y = self.max_scroll
            self.draw()

    def navigate(self, url, from_back=False, from_forward=False):
        """Navigate to URL"""
        url = resolve_url(self.url, url)

        if self.url:
            if from_forward:
                self.forward_history.append(self.url)
            elif not from_back:
                self.history.append(self.url)
                self.forward_history = []  # Clear forward on new navigation
        self.url = url
        self.address_text = url
        self.scroll_y = 0
        self.editing_url = False

        vibe.window_set_title(self.wid, "Loading...")
        self.draw_address_bar()
        vibe.window_invalidate(self.wid)
        vibe.sched_yield()

        status, body = fetch_url(url)

        # Handle redirects
        if status in (301, 302, 307, 308):
            self.navigate(body)
            return

        if status != 200:
            body = "<html><body><h1>Error " + str(status) + "</h1><p>" + body + "</p></body></html>"

        self.current_html = body
        root = parse_html(body)
        self.blocks, self.links, self.content_height = self.layout_document(root)

        self.max_scroll = max(0, self.content_height - self.content_h)

        title_elem = find_element(root, 'title')
        title = get_all_text(title_elem) if title_elem else url
        vibe.window_set_title(self.wid, title[:40])

        self.draw()

    def back(self):
        """Go back in history"""
        if len(self.history) > 0:
            self.forward_history.append(self.url)
            url = self.history.pop()
            self.url = ''
            self.navigate(url, from_back=True)

    def forward(self):
        """Go forward in history"""
        if len(self.forward_history) > 0:
            self.history.append(self.url)
            url = self.forward_history.pop()
            self.url = ''
            self.navigate(url, from_forward=True)

    def refresh(self):
        """Reload current page"""
        if self.url:
            url = self.url
            self.url = ''  # Clear so it doesn't add to history
            self.navigate(url)

    def draw(self):
        """Draw the browser UI"""
        self.draw_address_bar()
        render(self.wid, self.blocks, self.scroll_y, self.content_y, self.content_h, self.win_w, self.win_h)
        self.draw_scrollbar()
        vibe.window_invalidate(self.wid)

    def draw_address_bar(self):
        """Draw address bar or nav buttons"""
        if self.kiosk_mode:
            # Draw minimal nav bar with just back/forward buttons
            vibe.window_fill_rect(self.wid, 0, 0, self.win_w, 20, LIGHT_GRAY)
            # Back button
            vibe.window_fill_rect(self.wid, 4, 2, 16, 16, WHITE)
            vibe.window_draw_rect(self.wid, 4, 2, 16, 16, BLACK)
            vibe.window_draw_string(self.wid, 6, 2, "<", BLACK, WHITE)
            # Forward button
            vibe.window_fill_rect(self.wid, 24, 2, 16, 16, WHITE)
            vibe.window_draw_rect(self.wid, 24, 2, 16, 16, BLACK)
            vibe.window_draw_string(self.wid, 26, 2, ">", BLACK, WHITE)
            return

        vibe.window_fill_rect(self.wid, 0, 0, self.win_w, ADDRESS_BAR_H, LIGHT_GRAY)

        # Back button
        vibe.window_fill_rect(self.wid, 4, 4, 16, 16, WHITE)
        vibe.window_draw_rect(self.wid, 4, 4, 16, 16, BLACK)
        vibe.window_draw_string(self.wid, 6, 4, "<", BLACK, WHITE)

        # Forward button
        vibe.window_fill_rect(self.wid, 24, 4, 16, 16, WHITE)
        vibe.window_draw_rect(self.wid, 24, 4, 16, 16, BLACK)
        vibe.window_draw_string(self.wid, 26, 4, ">", BLACK, WHITE)

        # Refresh button
        vibe.window_fill_rect(self.wid, 44, 4, 16, 16, WHITE)
        vibe.window_draw_rect(self.wid, 44, 4, 16, 16, BLACK)
        vibe.window_draw_string(self.wid, 46, 4, "O", BLACK, WHITE)

        # Address bar (shifted right for buttons)
        vibe.window_fill_rect(self.wid, 64, 4, self.win_w - 68, 16, WHITE)
        vibe.window_draw_rect(self.wid, 64, 4, self.win_w - 68, 16, BLACK)

        # Calculate max displayable chars based on window width
        max_chars = (self.win_w - 76) // 8
        display_url = self.address_text
        if len(display_url) > max_chars:
            display_url = display_url[:max_chars] + '...'
        vibe.window_draw_string(self.wid, 68, 4, display_url, BLACK, WHITE)

        # Draw cursor if editing
        if self.editing_url:
            cursor_x = 68 + self.cursor_pos * 8
            vibe.window_fill_rect(self.wid, cursor_x, 6, 1, 12, BLACK)

    def draw_scrollbar(self):
        """Draw scrollbar if needed"""
        if self.max_scroll <= 0:
            return

        bar_x = self.win_w - SCROLLBAR_W
        bar_h = self.content_h

        thumb_h = max(20, bar_h * self.content_h // self.content_height)
        thumb_y = self.content_y + (bar_h - thumb_h) * self.scroll_y // self.max_scroll

        vibe.window_fill_rect(self.wid, bar_x, self.content_y, SCROLLBAR_W, bar_h, LIGHT_GRAY)
        vibe.window_fill_rect(self.wid, bar_x + 2, thumb_y, SCROLLBAR_W - 4, thumb_h, GRAY)

    def handle_click(self, x, y):
        """Handle mouse click"""
        # Check kiosk mode nav buttons
        if self.kiosk_mode and y < 20:
            # Back button
            if x >= 4 and x < 20 and y >= 2 and y < 18:
                self.back()
                return
            # Forward button
            if x >= 24 and x < 40 and y >= 2 and y < 18:
                self.forward()
                return
            return

        # Check toolbar buttons (normal mode)
        if not self.kiosk_mode and y < ADDRESS_BAR_H:
            # Back button
            if x >= 4 and x < 20 and y >= 4 and y < 20:
                self.back()
                return
            # Forward button
            if x >= 24 and x < 40 and y >= 4 and y < 20:
                self.forward()
                return
            # Refresh button
            if x >= 44 and x < 60 and y >= 4 and y < 20:
                self.refresh()
                return
            # Click on address bar
            if x >= 64:
                self.editing_url = True
                self.cursor_pos = len(self.address_text)
                self.draw_address_bar()
                vibe.window_invalidate(self.wid)
                return

        # Check scrollbar click
        if x >= self.win_w - SCROLLBAR_W and y >= self.content_y:
            self.scrollbar_dragging = True
            self.scrollbar_drag_start_y = y
            self.scrollbar_drag_start_scroll = self.scroll_y
            return

        # Check links
        content_y = y - self.content_y + self.scroll_y
        for lx, ly, lw, lh, href in self.links:
            if lx <= x < lx + lw and ly <= content_y < ly + lh:
                self.navigate(href)
                return

    def handle_mouse_up(self, x, y):
        """Handle mouse release"""
        self.scrollbar_dragging = False

    def handle_mouse_move(self, x, y):
        """Handle mouse movement"""
        if self.scrollbar_dragging and self.max_scroll > 0:
            dy = y - self.scrollbar_drag_start_y
            # Calculate how much to scroll based on drag distance
            bar_h = self.content_h
            thumb_h = max(20, bar_h * self.content_h // self.content_height)
            track_range = bar_h - thumb_h
            if track_range > 0:
                scroll_delta = (dy * self.max_scroll) // track_range
                new_scroll = self.scrollbar_drag_start_scroll + scroll_delta
                new_scroll = max(0, min(self.max_scroll, new_scroll))
                if new_scroll != self.scroll_y:
                    self.scroll_y = new_scroll
                    self.draw()

    def handle_key(self, key):
        """Handle keyboard input"""
        if self.editing_url:
            if key == 13 or key == 10:  # Enter (CR or LF)
                self.editing_url = False
                self.navigate(self.address_text)
            elif key == 27:  # Escape
                self.editing_url = False
                self.address_text = self.url
                self.draw_address_bar()
                vibe.window_invalidate(self.wid)
            elif key == 8 or key == 127:  # Backspace
                if self.cursor_pos > 0:
                    self.address_text = self.address_text[:self.cursor_pos-1] + self.address_text[self.cursor_pos:]
                    self.cursor_pos -= 1
                    self.draw_address_bar()
                    vibe.window_invalidate(self.wid)
            elif key >= 32 and key < 127:
                self.address_text = self.address_text[:self.cursor_pos] + chr(key) + self.address_text[self.cursor_pos:]
                self.cursor_pos += 1
                self.draw_address_bar()
                vibe.window_invalidate(self.wid)
            return True  # Stay running while editing
        else:
            if key == 27:  # ESC
                return False
            elif key == 0x100:  # UP
                self.scroll_y = max(0, self.scroll_y - 20)
                self.draw()
            elif key == 0x101:  # DOWN
                self.scroll_y = min(self.max_scroll, self.scroll_y + 20)
                self.draw()
            elif key == 0x109:  # PGUP
                self.scroll_y = max(0, self.scroll_y - self.content_h)
                self.draw()
            elif key == 0x10A:  # PGDN
                self.scroll_y = min(self.max_scroll, self.scroll_y + self.content_h)
                self.draw()
        return True

    def run(self, initial_url):
        """Main event loop"""
        self.wid = vibe.window_create(50, 30, WIN_W, WIN_H, "Kivi")
        if self.wid < 0:
            vibe.puts("Failed to create window\n")
            return 1

        self.navigate(initial_url)

        running = True
        while running:
            event = vibe.window_poll(self.wid)

            if event:
                etype, d1, d2, d3 = event

                if etype == vibe.WIN_EVENT_CLOSE:
                    running = False
                elif etype == vibe.WIN_EVENT_MOUSE_DOWN:
                    self.handle_click(d1, d2)
                elif etype == vibe.WIN_EVENT_MOUSE_UP:
                    self.handle_mouse_up(d1, d2)
                elif etype == vibe.WIN_EVENT_MOUSE_MOVE:
                    self.handle_mouse_move(d1, d2)
                elif etype == vibe.WIN_EVENT_KEY:
                    if not self.handle_key(d1):
                        running = False
                elif etype == vibe.WIN_EVENT_RESIZE:
                    self.handle_resize(d1, d2)

            vibe.sched_yield()

        vibe.window_destroy(self.wid)
        return 0

# ============================================================================
# Main
# ============================================================================

def main():
    import sys

    # Default URL - welcome page
    url = 'about:home'
    kiosk_mode = False

    # Check arguments
    for arg in sys.argv[1:]:
        if arg == '--kiosk':
            kiosk_mode = True
        elif arg.startswith('http://') or arg.startswith('https://') or arg.startswith('file://') or arg.startswith('about:'):
            url = arg

    browser = Browser(kiosk_mode=kiosk_mode)
    return browser.run(url)

main()

