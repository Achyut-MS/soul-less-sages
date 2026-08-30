---
title: Advanced Markdown Test
description: Tests extended Markdown features
---

# Advanced Markdown Features

## 1. Footnotes

Markdown can support footnotes.[^first]

Here is another reference.[^second]

[^first]: This is the first footnote.
[^second]: This is the second footnote with **formatting**.

---

## 2. Collapsible Content

<details>
<summary><strong>Click to expand this section</strong></summary>

### Hidden Heading

This content is inside a collapsible section.

- Hidden item 1
- Hidden item 2

```python
print("Hidden code")
```

</details>

---

## 3. GitHub-Style Alerts

> [!NOTE]
> This is a note.

> [!TIP]
> This is a useful tip.

> [!IMPORTANT]
> This information is important.

> [!WARNING]
> Be careful before continuing.

> [!CAUTION]
> This action could be dangerous.

---

## 4. Mathematics

Inline math:

$E = mc^2$

Quadratic equation:

$$
x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

Pythagorean theorem:

$$
a^2 + b^2 = c^2
$$

---

## 5. Mermaid Flowchart

```mermaid
flowchart TD
    A([Start]) --> B{Does Mermaid work?}
    B -->|Yes| C[Great!]
    B -->|No| D[Renderer does not support Mermaid]
    C --> E([End])
    D --> E
```

---

## 6. Mermaid Sequence Diagram

```mermaid
sequenceDiagram
    participant User
    participant App
    participant Server

    User->>App: Open Markdown file
    App->>Server: Request data
    Server-->>App: Return data
    App-->>User: Display content
```

---

## 7. Mermaid Class Diagram

```mermaid
classDiagram
    class User {
        +String name
        +openFile()
    }

    class MarkdownFile {
        +String filename
        +render()
    }

    User --> MarkdownFile
```

---

## 8. Mermaid Pie Chart

```mermaid
pie title Markdown Test Results
    "Supported" : 80
    "Unsupported" : 20
```

---

## 9. Definition List

Markdown
: A lightweight markup language.

Renderer
: Software that converts Markdown into formatted output.

---

## 10. HTML Table

<table>
<tr>
<th>Feature</th>
<th>HTML Test</th>
</tr>
<tr>
<td>Bold</td>
<td><strong>Works?</strong></td>
</tr>
<tr>
<td>Italic</td>
<td><em>Works?</em></td>
</tr>
</table>

---

## 11. Raw HTML

<div style="padding: 10px; border: 1px solid gray;">
<strong>HTML container test</strong><br>
If this looks styled, your renderer allows HTML and possibly inline CSS.
</div>

---

## 12. Emoji Shortcodes

:rocket: :fire: :smile: :heart:

Shortcode support depends on the Markdown renderer.

---

## Advanced Test Complete

Check which sections render correctly and which sections remain plain text.
