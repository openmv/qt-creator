# -*- coding: utf-8 -*-
import click

__version__ = '1.2.0'


@click.command(name='tree')
@click.pass_context
def tree(ctx):
    """show the command tree of your CLI"""
    root_cmd = _build_command_tree(ctx.find_root().command)
    _print_tree(root_cmd)


class _CommandWrapper(object):
    def __init__(self, command=None, children=None):
        self.command = command
        self.children = []

    @property
    def name(self):
        return self.command.name

    def __repr__(self):
        return '{{_CommandWrapper {}}}'.format(self.name)


def _build_command_tree(click_command):
    wrapper = _CommandWrapper(click_command)

    if isinstance(click_command, click.core.Group):
        for _, cmd in click_command.commands.items():
            if not getattr(cmd, "hidden", False): 
                wrapper.children.append(_build_command_tree(cmd))

    return wrapper


def _print_tree(command, depth=0, is_last_item=False, is_last_parent=False):
    if depth == 0:
        prefix = ''
        tree_item = ''
    else:
        prefix = '    ' if is_last_parent else '│   '
        tree_item = '└── ' if is_last_item else '├── '

    line = prefix * (depth - 1) + tree_item + command.name
    doc = _get_truncated_docstring(command.command)
    if doc:
        line += ' - {}'.format(doc)

    click.echo(line)

    for i, child in enumerate(sorted(command.children, key=lambda x: x.name)):
        _print_tree(child,
                    depth=(depth + 1),
                    is_last_item=(i == (len(command.children) - 1)),
                    is_last_parent=is_last_item)


def _get_truncated_docstring(command):
    doc = command.__doc__

    if not doc:
        return None

    lines = doc.split("\n")
    if not lines:
        return None

    for line in lines:
        line = line.strip()
        if not line:
            continue

        return line[:80] + ' ...' if len(line) > 80 else line

    return None
