#!/usr/bin/env python2
import platform
import sys, os, os.path
from time import sleep
import glob, urllib2, json, shutil
from base64 import b64decode

# Top download dir
DATA_DIR    = 'data'
# Use the following format: [ (dst_dir, [src_dir/file1, src_dir/file2, ...]), (dst_dir, [src_dir]), ...]
DATA_FILES = [('udev', ['udev']),
              ('firmware', ['firmware']),
              ('examples', ['usr/examples']),
              ('util', ['usr/pydfu.py', 'usr/openmv-cascade.py',]),]

DATA_URL = 'https://api.github.com/repos/openmv/openmv/contents/%s?ref=%s'
RELEASE_URL = 'https://api.github.com/repos/openmv/openmv/releases/latest'

def fetch_url(url, json_resp):
    buf = None
    print("fetch: %s"%(url))

    try:
        url = urllib2.urlopen(url)
        buf = url.read()
        url.close()
    except Exception as e:
        print ("Faild to fetch file %s!"%(url))
        raise e

    if (json_resp == False):
        return buf

    return json.loads(buf)

def fetch_object(root, path, tag):
    response = fetch_url(DATA_URL%(path, tag), True)

    if type(response)!=list: #file
        # Add to a list so the following works nicely
        response = [response]

    for entry in response:
        if (entry['type']=='file'):
            if (entry.get('content')):
                # Top entry, read content directly
                buf = b64decode(entry['content'])
            else:
                # Entry in a directory, use download url
                buf = fetch_url(entry['download_url'], False)

            # write file
            with open(os.path.join(root, entry['name']), 'wb') as out:
                out.write(buf)
        else:
            # create directory
            path = os.path.join(root, entry['name'])
            os.mkdir(path)
            fetch_object(path, entry['path'], tag)

if __name__ == "__main__":
    # cleanup
    if os.path.exists(DATA_DIR):
        shutil.rmtree(DATA_DIR)

    # create root directory
    os.mkdir(DATA_DIR)

    tag = fetch_url(RELEASE_URL, True)['tag_name']

    for dir, files in DATA_FILES:
        dir_path = os.path.join(DATA_DIR, dir)
        # create directory
        os.mkdir(dir_path)

        # fetch files
        for f_path in files:
            fetch_object(dir_path, f_path, tag)
