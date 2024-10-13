import sys
import os
sys.path.insert(0, '')
python_path = os.getenv('PYTHONPATH')
if python_path:
    sys.path = python_path.split(os.pathsep) + sys.path
