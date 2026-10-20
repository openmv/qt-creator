# OpenMV IDE: changes to the embedded Python language server, applied at runtime.
#
# The IDE puts this folder first on the language server's PYTHONPATH, so Python
# runs this file (as `sitecustomize`) when the server starts. It patches Jedi in
# memory; the pylsp bundles next to this folder (windows/, mac/, linux-*/) stay
# exactly as downloaded, so updating them needs no re-applying of anything.
#
# Each patch checks that what it replaces is still there and skips itself with a
# warning on the server's stderr if not (a Jedi update reshaped it), leaving the
# server working. check_patches.py tests that they all took effect.
#
# Why: OpenMV's stubs (share/qtcreator/html/stubs) describe the camera's
# MicroPython API. Jedi treats a stub as a partial description of a desktop
# module of the same name and fills in the rest from it, and it emulates CPython
# behavior MicroPython doesn't have. Both offered code the camera can't run.

import functools
import sys


def _warn(message):
    sys.stderr.write("OpenMV IDE pylsp patches: %s\n" % message)


def _patch(name):
    """Runs a patch, reporting (not raising) a failure."""

    def decorator(function):
        try:
            function()
        except Exception as error:  # noqa: BLE001 -- a patch must never stop the server
            _warn("%s not applied: %r" % (name, error))
        return function

    return decorator


# 1. No CPython name mangling. Jedi hides `__name` (non-dunder) class members
#    from outside the class, as CPython mangles them to `_Class__name`.
#    MicroPython doesn't, so csi.CSI.__read_reg/__write_reg must complete.
@_patch("name mangling")
def _no_name_mangling():
    from jedi.inference.value.klass import ClassFilter

    if not hasattr(ClassFilter, "_access_possible"):
        raise AttributeError("ClassFilter._access_possible")
    ClassFilter._access_possible = lambda self, name: True


# 2. A stub is the module's whole API: completion adds no names from the desktop
#    module behind it (re.findall, json.JSONDecoder: not on the camera).
@_patch("stub-only completion")
def _stub_only_completion():
    from jedi.api import completion
    from jedi.inference.gradual.conversion import convert_values

    original = completion.complete_trailer
    TreeInstance = completion.TreeInstance
    complete_getattr = completion._complete_getattr

    @functools.wraps(original)
    def complete_trailer(user_context, values):
        completion_names = []
        for value in values:
            for filter in value.get_filters(origin_scope=user_context.tree_node):
                completion_names += filter.values()
            if not value.is_stub() and isinstance(value, TreeInstance):
                completion_names += complete_getattr(user_context, value)

        # Only values without a stub get their implementation's names.
        python_values = convert_values([v for v in values if not v.is_stub()])
        for c in python_values:
            if c not in values:
                for filter in c.get_filters(origin_scope=user_context.tree_node):
                    completion_names += filter.values()
        return completion_names

    completion.complete_trailer = complete_trailer


# 3. Stubs first for go to definition, hover and help (pylsp's plugins don't ask
#    for them): they land on the camera's API, not the desktop source.
@_patch("stubs first")
def _stubs_first():
    from jedi.api import Script

    def prefer(method):
        @functools.wraps(method)
        def wrapper(self, line=None, column=None, *, only_stubs=False, prefer_stubs=None, **kwargs):
            if prefer_stubs is None:
                prefer_stubs = not only_stubs
            return method(self, line, column, only_stubs=only_stubs,
                          prefer_stubs=prefer_stubs, **kwargs)

        return wrapper

    for name in ("goto", "infer"):
        method = getattr(Script, name)
        setattr(Script, name, prefer(method))


# 4. A stub's own docstring wins (the camera's docs); the implementation's only
#    when the stub has none.
@_patch("stub docstrings")
def _stub_docstrings():
    from jedi.inference import names
    from jedi.inference.names import TreeNameDefinition

    clean = names.clean_scope_docstring
    original = TreeNameDefinition.py__doc__

    @functools.wraps(original)
    def py__doc__(self):
        if self.api_type in ("function", "class", "property") \
                and self.parent_context.get_root_context().is_stub():
            own = clean(self.tree_name.get_definition())
            if own:
                return own
        return original(self)

    TreeNameDefinition.py__doc__ = py__doc__


# 5. Only a stub's own submodules (not re's _compiler, _parser...).
@_patch("stub submodules")
def _stub_submodules():
    from jedi.inference.gradual.stub_value import StubModuleValue
    from jedi.inference.value import ModuleValue

    if "sub_modules_dict" not in StubModuleValue.__dict__:
        raise AttributeError("StubModuleValue.sub_modules_dict")
    StubModuleValue.sub_modules_dict = lambda self: ModuleValue.sub_modules_dict(self)


# This file takes the name `sitecustomize`: run the one it shadows, if any.
def _chain_sitecustomize():
    import importlib.machinery
    import importlib.util
    import os

    here = os.path.dirname(os.path.abspath(__file__))
    path = [p for p in sys.path if os.path.abspath(p or os.curdir) != here]
    spec = importlib.machinery.PathFinder.find_spec("sitecustomize", path)
    if spec and spec.loader and spec.origin != os.path.abspath(__file__):
        try:
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        except Exception as error:  # noqa: BLE001
            _warn("the original sitecustomize failed: %r" % (error,))


_chain_sitecustomize()
