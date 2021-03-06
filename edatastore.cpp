/*
 * filename: edatastore.cpp
 * describe: First of all, I want you to know that the goal of EDS (Embedded
 *           Data Store) in software industry is to
 *
 *           Make Efficiency Great Again!
 *
 *           The algorithm complexity of EDS even in the WORST cases, is still
 *                   O(log n)
 *           for insert, query and delete operations.
 * 
 *           The underlying technique of EDS - my customized B-Tree algorithm
 *           highly optimized for the state-of-the-art SSD Disk system is ba-
 *           sically implemented in C language, only wrapped by a thin C++
 *           layer, which is the interface for the EDS engine -  EDatastore
 *           which is written in STANDARD C++ without any third party depen-
 *           dency (libraries).
 *
 *           Generally, each datastore generated by EDS has two files with the
 *           extension name .eds and .idx no matter how many tables(objects)
 *           there are inside it, one is the data file another is the index
 *           file. So it's very convenient for you to migrate and deploy your
 *           applications to different machines and/or different platforms -
 *           just copy-and-paste the two files for each datastore. Of course,
 *           EDS supports multiple datastores running in one single application
 *           as well.
 *
 *           EDS is written in OO and for OO, thus it removes the ORM burden
 *           which works as a "middleman" between the applications and the tra-
 *           ditional RDBMS to make it more efficient and much easier for modern
 *           applications development. Objects ARE tables in EDS, many objects
 *           consist of a Datastore, neither SQL nor conversion between applica-
 *           tions and RDBMS are needed. Yes, it's just so simple and so direct
 *           and so INTUITIVE.
 * Author:   Jerry Sun <jerysun0818@gmail.com>
 * Date:     August 17, 2015
 * Remark:   EDS is ongoing, still evolves... The goal of EDS is not to replace
 *           the RDBMS that's suitable for large complex database applications
 *           and people who like and are good at SQL. EDS, just like the name
 *           hints, it aims at embedded system and applications, but so far it
 *           has flourished in desktop applications as well which delivery chan-
 *           nel are via Internet, because as a developer or software vendor:

 *           you don't have to buy and install the cumbersome relational database
 *           servers onsite for your clients;
 *           
 *           you don't have to ask your clients to buy and install the cumbersome
 *           relational database servers by themselves;
 *           
 *           you don't have to buy the dedicated relational database servers and
 *           install them at your company for your clients' connections and access
 *
 *           EDS wins in the efficiency and affinity with the frontline
 *           application developers.
 * Linkedin: http://nl.linkedin.com/in/jerysun
 * Website:  https://sites.google.com/site/geekssmallworld
 * Github:   https://github.com/jerysun/
 */

/* Copyright (c) 2017 Dates Great, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "stdafx.h"
#include <new.h>
#include <stdlib.h>
#include <string>
#include <algorithm>
#include "edatastore.h"

#pragma warning (disable: 4267)

EDatastore *EDatastore::opendatastore; // latest open datastore

// construct a EDatastore datastore
EDatastore::EDatastore(const std::string &name) : datafile(name), indexfile(name) {
  rebuildnode = 0;
  previousdatastore = opendatastore;
  opendatastore = this;
}

// close the EDatastore datastore
EDatastore::~EDatastore() {
  EdsBtree *bt = btrees.FirstEntry();
  while (bt != 0) {
    delete bt;
    bt = btrees.NextEntry();
  }
  Class *cls = classes.FirstEntry();
  while (cls != 0) {
    delete[] cls->classname;
    delete cls;
    cls = classes.NextEntry();
  }
  opendatastore = previousdatastore;
}

// read an object header record
void EDatastore::GetObjectHeader(ObjAddr nd, ObjectHeader &objhdr) {
  // constructing this node seeks to the first data byte
  Node(&datafile, nd);
  datafile.ReadData(&objhdr, sizeof(ObjectHeader));
}

Class *EDatastore::Registration(const Serialize &pcls) {
  Class *cls = classes.FirstEntry();
  while (cls != 0) {
    const char *ty = typeid(pcls).name();
    if (strcmp(cls->classname, ty) == 0) break;
    cls = classes.NextEntry();
  }
  return cls;
}

bool EDatastore::FindClass(Class *cls, NodeNbr *nd) {
  char classname[classnamesize];
  ClassID cid = 0;
  if (!indexfile.NewFile()) {
    Node tmpnode;
    NodeNbr nx = 1;
    // locate the class header
    while (nx != 0) {
      tmpnode = Node(&indexfile, nx);
      indexfile.ReadData(classname, classnamesize);
      if (strcmp(classname, cls->classname) == 0) {
        cls->headeraddr = indexfile.FilePosition();
        cls->classid = cid;
        return true;
      }
      // this node is not the class header
      cid++;
      nx = tmpnode.NextNode();
    }
    if (nd != 0) {
      *nd = indexfile.NewNode();
      tmpnode.SetNextNode(*nd);
    }
  }
  cls->classid = cid;
  return false;
}

ClassID EDatastore::GetClassID(const char *classname) {
  Class cls(const_cast<char *>(classname));
  FindClass(&cls);
  return cls.classid;
}

void EDatastore::AddClassToIndex(Class *cls) {
  NodeNbr nd = 0;

  if (FindClass(cls, &nd) == false) {
    indexfile.ResetNewFile();
    nd = nd ? nd : indexfile.NewNode();
    // build the class header for new class
    Node tmpnode(&indexfile, nd);

    //  write class name into class record
    indexfile.WriteData(cls->classname, classnamesize);

    // save disk address of tree headers
    cls->headeraddr = indexfile.FilePosition();

    // pad the residual node space
    int residual = nodedatalength - classnamesize;
    char *residue = new char[residual];
    memset(residue, 0, residual);
    indexfile.WriteData(residue, residual);
    delete residue;

    tmpnode.MarkNodeChanged();
  }
}

// register a class's indexes with the datastore manager
void EDatastore::RegisterIndexes(Class *cls,
  const Serialize &pcls) throw(ZeroLengthKey) {
  Serialize &cl = const_cast<Serialize &>(pcls);
  EdsKey *key = cl.keys.FirstEntry();
  while (key != 0) {
    if (key->GetKeyLength() == 0) {
      throw ZeroLengthKey();
    }
    EdsBtree *bt = new EdsBtree(indexfile, cls, key);
    bt->SetClassIndexed(cls);
    btrees.AppendEntry(bt);
    key = cl.keys.NextEntry();
  }
}

// register a serialize class with the datastore manager
ClassID EDatastore::RegisterClass(const Serialize &pcls) {
  Class *cls = Registration(pcls);
  if (cls == 0) {
    cls = new Class;
    const char *cn = typeid(pcls).name();
    cls->classname = new char[strlen(cn) + 1];
    strcpy(cls->classname, cn);

    // search the index file for the class
    AddClassToIndex(cls);

    // register the indexes
    RegisterIndexes(cls, pcls);

    classes.AppendEntry(cls);
  }
  return cls->classid;
}

// Serialize base class member functions
Serialize *Serialize::objconstructed = 0;
Serialize *Serialize::objdestroyed = 0;
bool Serialize::usingnew = false;

// common constructor code
void Serialize::BuildObject() throw(NoDatastore) {
  if (EDatastore::OpenDatastore() == 0) {
    throw NoDatastore();
  }

  prevconstructed = objconstructed;
  objconstructed = this;
  changed = false;
  deleted = false;
  newobject = false;
  loaded = false;
  saved = false;
  offset = 0;
  indexcount = 0;
  node = 0;
  objectaddress = 0;
  instances = 0;
}

// destructor
Serialize::~Serialize() throw(NotLoaded, NotSaved, MustDestroy) {
  if (EDatastore::OpenDatastore() == 0)
    throw NoDatastore();
  RemoveObject();
  keys.ClearList();
  delete node;

  if (!loaded) {
    throw NotLoaded();
  }
  if (!saved) {
    throw NotSaved();
  }
  if (instances != 0) {
    throw MustDestroy();
  }
}

void Serialize::Destroy(Serialize *pp) {
  if (pp != 0) {
    if (pp->instances == 0) {
      delete (pp);
    }
    else {
      --(pp->instances);
    }
  }
}

Serialize *Serialize::ObjectBeingConstructed() throw(NotInConstructor) {
  Serialize *oc = objconstructed;
  if (oc == 0) {
    throw NotInConstructor();
  }
  return oc;
}

Serialize *Serialize::ObjectBeingDestroyed() throw(NotInDestructor) {
  Serialize *dc = objdestroyed;
  if (dc == 0) {
    throw NotInDestructor();
  }
  return dc;
}

// search the collected Btrees for this key's index
EdsBtree *Serialize::FindIndex(EdsKey *key) {
  EdsBtree *bt = 0;
  if (key == 0) {
    key = keys.FirstEntry();
  }

  if (key != 0) {
    bt = edatastore->btrees.FirstEntry();
    while (bt != 0) {
      const char *ty = typeid(*this).name();
      if (strcmp(ty, bt->ClassIndexed()->classname) == 0) {
        if (bt->Indexno() == key->indexno) break;
      }
      bt = edatastore->btrees.NextEntry();
    }
  }
  return bt;
}

// remove copies of the original keys
void Serialize::RemoveOrgKeys() {
  EdsKey *ky = orgkeys.FirstEntry();
  while (ky != 0) {
    delete ky;
    ky = orgkeys.NextEntry();
  }
  orgkeys.ClearList();
}

//  ---------------- record the object's state
void Serialize::RecordObject() {
  // remove object from the list of instantiated objects
  RemoveOrgKeys();
  // remove copies of the original keys
  edatastore->objects.RemoveEntry(this);
  // put the object's address in a edatastore list of
  //      instantiated objects
  edatastore->objects.AppendEntry(this);
  // make copies of the original keys for later update
  EdsKey *key = keys.FirstEntry();

  while (key != 0) {
    EdsKey *ky = key->MakeKey();
    *ky = *key;
    orgkeys.AppendEntry(ky);
    // instantiate the index b-tree (if not already)
    FindIndex(ky);
    key = keys.NextEntry();
  }
}

//  ---- remove the record of the object's state
void Serialize::RemoveObject() {
  // remove object from the list of instantiated objects
  edatastore->objects.RemoveEntry(this);
  // remove copies of the original keys
  RemoveOrgKeys();
}

void Serialize::TestDuplicateObject() throw(Serialize *) {
  if (objectaddress != 0) {
    // search for a previous instance of this object
    Serialize *obj = edatastore->objects.FirstEntry();
    while (obj != 0) {
      if (objectaddress == obj->objectaddress) {
        // object already instantiated
        obj->instances++;
        saved = true;
        throw obj;
      }
      obj = edatastore->objects.NextEntry();
    }
  }
}

// called from derived constructor after all construction
void Serialize::LoadObject(ObjAddr nd) {
  loaded = true;
  objconstructed = 0;
  objhdr.classid = edatastore->RegisterClass(*this);
  objectaddress = nd;
  if (edatastore->rebuildnode) {
    objectaddress = edatastore->rebuildnode;
  }

  if (objectaddress == 0) {
    // position at object's node
    SearchIndex(keys.FirstEntry());
  }
  ReadDataMembers();
  objconstructed = prevconstructed;
}

// write the object to the datastore
void Serialize::ObjectOut() {
  Serialize *hold = objdestroyed;
  objdestroyed = this;
  // tell object to write its data members
  Write();
  objdestroyed = hold;
  // pad the last node
  int padding = nodedatalength - offset;
  if (padding) {
    char *pads = new char[padding];
    memset(pads, 0, padding);
    edatastore->datafile.WriteData(pads, padding);
    delete pads;
  }

  NodeNbr nx = node->NextNode();
  node->SetNextNode(0);
  delete node;
  node = 0;
  // if node was linked, object got shorter
  while (nx != 0) {
    Node nd(&edatastore->datafile, nx);
    nx = nd.NextNode();
    nd.MarkNodeDeleted();
  }
  edatastore->datafile.Seek(filepos);
}

// write the object's node header
void Serialize::WriteObjectHeader() {
  // write the relative node number and class id
  edatastore->datafile.WriteData(&objhdr, sizeof(ObjectHeader));
  offset = sizeof(ObjectHeader);
}

// read the object's node header
void Serialize::ReadObjectHeader() {
  // read the relative node number and class id
  edatastore->datafile.ReadData(&objhdr, sizeof(ObjectHeader));
  offset = sizeof(ObjectHeader);
}

// called from derived destructor before all destruction, a new or
// existing object is being saved
void Serialize::SaveObject() throw(NoDatastore) {
  if (EDatastore::OpenDatastore() == 0) {
    throw NoDatastore();
  }

  saved = true;
  if (edatastore->rebuildnode) {
    AddIndexes();
    return;
  }

  if (newobject) {
    if (!deleted && ObjectExists()) {
      AddIndexes();
      PositionNode();
      ObjectOut();
      RecordObject();
    }
  }
  else if (deleted || changed && ObjectExists()) {
    // position the edatastore file at the object's node
    PositionNode();
    if (deleted) {
      // delete the object's nodes from the datastore
      while (node != 0) {
        node->MarkNodeDeleted();
        NodeNbr nx = node->NextNode();
        delete node;
        if (nx) {
          node = new Node(&edatastore->datafile, nx);
        }
        else {
          node = 0;
        }
      }
      DeleteIndexes();
      objectaddress = 0;
    }
    else {
      // tell object to write its data members
      ObjectOut();
      // update the object's indexes
      UpdateIndexes();
      RecordObject();
    }
    edatastore->datafile.Seek(filepos);
  }
  newobject = false;
  deleted = false;
  changed = false;
}

// read one data member of the object from the datastore
void Serialize::EdsReadObject(void *buf, int length) {
  while (node != 0 && length > 0) {
    if (offset == nodedatalength) {
      NodeNbr nx = node->NextNode();
      delete node;
      node = nx ? new Node(&edatastore->datafile, nx) : 0;
      ReadObjectHeader();
    }

    if (node != 0) {
      int len = std::min(length, static_cast<int>(nodedatalength - offset));
      edatastore->datafile.ReadData(buf, len);
      buf = reinterpret_cast<char *>(buf) + len;
      offset += len;
      length -= len;
    }
  }
}

// write one data member of the object to the datastore
void Serialize::EdsWriteObject(const void *buf, int length) {
  while (node != 0 && length > 0) {
    if (offset == nodedatalength) {
      NodeNbr nx = node->NextNode();
      if (nx == 0) {
        nx = edatastore->datafile.NewNode();
      }

      node->SetNextNode(nx);
      delete node;
      node = new Node(&edatastore->datafile, nx);
      WriteObjectHeader();
      objhdr.ndnbr++;
    }
    int len = std::min(length, static_cast<int>(nodedatalength - offset));
    edatastore->datafile.WriteData(buf, len);
    buf = reinterpret_cast<const char *>(buf) + len;
    offset += len;
    length -= len;
  }
}

void Serialize::ReadStrObject(std::string &str) {
  int len;
  EdsReadObject(&len, sizeof(int));
  char *s = new char[len + 1];
  EdsReadObject(s, len);
  s[len] = '\0';
  str = s;
  delete s;
}

void Serialize::WriteStrObject(const std::string &str) {
  int len = strlen(str.c_str());
  EdsWriteObject(&len, sizeof(int));
  EdsWriteObject(str.c_str(), len);
}

// add the index values to the object's index btrees
void Serialize::AddIndexes() {
  EdsKey *key = keys.FirstEntry();
  while (key != 0) {
    if (!key->isNullValue()) {
      EdsBtree *bt = FindIndex(key);
      key->fileaddr = objectaddress;
      bt->Insert(key);
    }
    key = keys.NextEntry();
  }
}

// update the index values in the object's index btrees
void Serialize::UpdateIndexes() {
  EdsKey *oky = orgkeys.FirstEntry();
  EdsKey *key = keys.FirstEntry();
  while (key != 0) {
    if (!(*oky == *key)) {
      // key value has changed, update the index
      EdsBtree *bt = FindIndex(oky);
      // delete the old
      if (!oky->isNullValue()) {
        oky->fileaddr = objectaddress;
        bt->Delete(oky);
      }
      // insert the new
      if (!key->isNullValue()) {
        key->fileaddr = objectaddress;
        bt->Insert(key);
      }
    }
    oky = orgkeys.NextEntry();
    key = keys.NextEntry();
  }
}

// delete the index values from the object's index btrees
void Serialize::DeleteIndexes() {
  EdsKey *key = orgkeys.FirstEntry();
  while (key != 0) {
    if (!key->isNullValue()) {
      EdsBtree *bt = FindIndex(key);
      key->fileaddr = objectaddress;
      bt->Delete(key);
    }
    key = orgkeys.NextEntry();
  }
}

// position the file to the specifed node number
void Serialize::PositionNode() throw(BadObjAddr) {
  filepos = edatastore->datafile.FilePosition();
  if (objectaddress) {
    delete node;
    node = new Node(&edatastore->datafile, objectaddress);
    offset = sizeof(ObjectHeader);
    ObjectHeader oh;
    edatastore->datafile.ReadData(&oh, sizeof(ObjectHeader));
    if (oh.ndnbr != 0 || oh.classid != objhdr.classid) {
      throw BadObjAddr();
    }
  }
}

// search the index for a match on the key
void Serialize::SearchIndex(EdsKey *key) {
  objectaddress = 0;
  if (key != 0 && !key->isNullValue()) {
    EdsBtree *bt = FindIndex(key);
    if (bt != 0 && bt->Find(key)) {
      if (key->indexno != 0) {
        EdsKey *bc;
        do {
          bc = bt->Previous();
        } while (bc != 0 && *bc == *key);
        key = bt->Next();
      }
      objectaddress = key->fileaddr;
    }
  }
}

// scan nodes forward to the first one of next object
void Serialize::ScanForward(NodeNbr nd) {
  ObjectHeader oh;
  while (nd++ < edatastore->datafile.HighestNode()) {
    edatastore->GetObjectHeader(nd, oh);
    if (oh.classid == objhdr.classid && oh.ndnbr == 0) {
      objectaddress = nd;
      break;
    }
  }
}

// scan nodes back to first one of the previous object
void Serialize::ScanBackward(NodeNbr nd) {
  ObjectHeader oh;
  while (--nd > 0) {
    edatastore->GetObjectHeader(nd, oh);
    if (oh.classid == objhdr.classid && oh.ndnbr == 0) {
      objectaddress = nd;
      break;
    }
  }
}

// find an object by a key value
Serialize &Serialize::FindObject(EdsKey *key) {
  RemoveObject();
  SearchIndex(key);
  ReadDataMembers();
  return *this;
}

// retrieve the current object in a key sequence
Serialize &Serialize::CurrentObject(EdsKey *key) {
  RemoveObject();
  EdsBtree *bt = FindIndex(key);
  if (bt != 0) {
    if ((key = bt->Current()) != 0) {
      objectaddress = key->fileaddr;
    }
    ReadDataMembers();
  }
  return *this;
}

// retrieve the first object in a key sequence
Serialize &Serialize::FirstObject(EdsKey *key) {
  RemoveObject();
  objectaddress = 0;
  EdsBtree *bt = FindIndex(key);
  if (bt == 0) { // keyless object
    ScanForward(0);
  }
  else if ((key = bt->First()) != 0) {
    objectaddress = key->fileaddr;
  }

  ReadDataMembers();
  return *this;
}

// retrieve the last object in a key sequence
Serialize &Serialize::LastObject(EdsKey *key) {
  RemoveObject();
  objectaddress = 0;
  EdsBtree *bt = FindIndex(key);
  if (bt == 0) { // keyless object
    ScanBackward(edatastore->datafile.HighestNode());
  }
  else if ((key = bt->Last()) != 0) {
    objectaddress = key->fileaddr;
  }

  ReadDataMembers();
  return *this;
}

// retrieve the next object in a key sequence
Serialize &Serialize::NextObject(EdsKey *key) {
  RemoveObject();
  ObjAddr oa = objectaddress;
  objectaddress = 0;
  EdsBtree *bt = FindIndex(key);

  if (bt == 0) { // keyless object
    ScanForward(oa);
  }
  else if ((key = bt->Next()) != 0) {
    objectaddress = key->fileaddr;
  }

  ReadDataMembers();
  return *this;
}

// retrieve the previous object in a key sequence
Serialize &Serialize::PreviousObject(EdsKey *key) {
  RemoveObject();
  ObjAddr oa = objectaddress;
  objectaddress = 0;
  EdsBtree *bt = FindIndex(key);

  if (bt == 0) { // keyless object
    ScanBackward(oa);
  } else if ((key = bt->Previous()) != 0) {
    objectaddress = key->fileaddr;
  }

  ReadDataMembers();
  return *this;
}

// read an object's data members
void Serialize::ReadDataMembers() {
  if (objectaddress != 0) {
    PositionNode();
    // tell object to read its data members
    Serialize *hold = objconstructed;
    objconstructed = this;
    Read();
    objconstructed = hold;
    delete node;
    node = 0;
    TestDuplicateObject();
    // post object instantiated and
    //     put secondary keys in table
    RecordObject();
    edatastore->datafile.Seek(filepos);
  }
}

// add an object to the EDatastore datastore
bool Serialize::AddObject() {
  newobject = (objectaddress == 0 && TestRelationships());
  if (newobject) {
    delete node; // (just in case)
    node = new Node(&edatastore->datafile, edatastore->datafile.NewNode());
    objectaddress = node->GetNodeNbr();
    WriteObjectHeader();
    objhdr.ndnbr++;
  }
  return newobject;
}

// mark a serialize object for change
bool Serialize::ChangeObject() {
  changed = TestRelationships();
  return changed;
}

// mark a serialize object for delete
bool Serialize::DeleteObject() {
  EdsKey *key = keys.FirstEntry();
  bool related = false;

  if (!key->isNullValue()) {
    // scan for other objects related to this one
    EdsBtree *bt = edatastore->btrees.FirstEntry();
    while (bt != 0 && !related) {
      // test only secondary keys
      if (bt->Indexno() != 0) {
        const type_info *relclass = bt->NullKey()->relatedclass;
        if (relclass != 0) {
          if (typeid(*this) == *relclass) {
            EdsKey *ky = bt->MakeKeyBuffer();
            if (ky->isObjectAddress()) {
              const ObjAddr *oa = ky->ObjectAddress();
              ObjectHeader oh;
              edatastore->GetObjectHeader(*oa, oh);
              if (oh.classid == objhdr.classid) {
                if (oh.ndnbr == 0) {
                  related = true;
                }
              }
            }
            else {
              ky->CopyKeyData(key);
              related = bt->Find(ky);
            }
          }
        }
      }
      bt = edatastore->btrees.NextEntry();
    }
  }
  deleted = !related;
  return deleted;
}

// test an object's relationships
//        return false if it is related to a
//        nonexistent object
//        return false if its primary key is already in use
bool Serialize::TestRelationships() {
  EdsKey *key = keys.FirstEntry();
  if (key == 0) return true;
  EdsBtree *bt;
  if (objectaddress == 0) {
    bt = FindIndex(key);
    if (bt != 0 && bt->Find(key)) {
      return false;
    }
  }
  bool unrelated = true;

  while ((key = keys.NextEntry()) != 0) {
    const type_info *relclass = key->relatedclass;
    if (key->isObjectAddress()) {
      const ObjAddr *oa = key->ObjectAddress();
      if (oa != 0) {
        ObjectHeader oh;
        edatastore->GetObjectHeader(*oa, oh);
        if (oh.ndnbr == 0) {
          // find classid of related class
          Class *cls = edatastore->classes.FirstEntry();
          const char *cn = relclass->name();
          while (cls != 0) {
            if (strcmp(cn, cls->classname) == 0) {
              break;
            }
            cls = edatastore->classes.NextEntry();
          }
          if (cls && cls->classid == oh.classid) {
            continue;
          }
        }
        unrelated = false;
      }
    }
    else if (!key->isNullValue() && relclass != 0) {
      const char *kc = relclass->name();
      bt = edatastore->btrees.FirstEntry();
      while (bt != 0 && unrelated) {
        // test only primary keys
        if (bt->Indexno() == 0) {
          // primary key of related class?
          const char *bc = bt->ClassIndexed()->classname;
          if (strcmp(bc, kc) == 0) {
            EdsKey *ky = bt->MakeKeyBuffer();
            ky->CopyKeyData(key);
            unrelated = bt->Find(ky);
          }
        }
        bt = edatastore->btrees.NextEntry();
      }
    }
  }
  return unrelated;
}