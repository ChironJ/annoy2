#ifndef __LMDB_FOREST_H
#define __LMDB_FOREST_H
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <memory.h>

#include <sys/stat.h> 
#include <fcntl.h>
#include <unistd.h>
#include <ios>
#include <iostream>
#include <fstream>
#include <string>
#ifdef __MINGW32__
 #include "mman.h"
 #include <windows.h>
#else
 #include <sys/mman.h>
#endif


#include <vector>
#include <map>
#include "lmdb.h"

#include "annoylib.h"
#include "protobuf/annoy.pb.h"

#define E(expr) CHECK((rc = (expr)) == MDB_SUCCESS, #expr)
#define RES(err, expr) ((rc = expr) == (err) || (CHECK(!rc, #expr), 0))
#define CHECK(test, msg) ((test) ? (void)0 : ((void)fprintf(stderr, \
"%s:%d: %s: %s\n", __FILE__, __LINE__, msg, mdb_strerror(rc)), abort()))


#define DBN_ROOT "root"
#define DBN_RAW "raw"
#define DBN_TREE "tree"





using namespace std;


/*
 a class to store a forest using key-value store LMDB. 
 
 1. Database DBN_RAW would use the id as the key for objs,
 store the raw vector values for each sample
 
 2. Database DBN_TREE would store the roots, the internal 
 nodes, as well as leaf nodes for the tree. 
 
    2.1 the keys for roots are "rt". the values are all the keys of the roots
    2.2 each node of the tree is stored as a protobuf obj tree_node
    2.3 leaf node would have an array of pointers to the raw data
 
 */

template<typename S, typename T>
class AnnoyIndexInterface {
public:
  virtual ~AnnoyIndexInterface() {};
  virtual void add_item(S item, const T* w) = 0;
  virtual void build(int q) = 0;
  virtual bool save(const char* filename) = 0;
  virtual void reinitialize() = 0;
  virtual void unload() = 0;
  virtual bool load(const char* filename) = 0;
  virtual T get_distance(S i, S j) = 0;
  virtual void get_nns_by_item(S item, size_t n, size_t search_k, vector<S>* result, vector<T>* distances) = 0;
  virtual void get_nns_by_vector(const T* w, size_t n, size_t search_k, vector<S>* result, vector<T>* distances) = 0;
  virtual S get_n_items() = 0;
  virtual void verbose(bool v) = 0;
  virtual void get_item(S item, vector<T>* v) = 0;
};

template<typename S, typename T, template<typename, typename, typename> class Distance, class Random>
class AnnoyIndex : public AnnoyIndexInterface<S, T> {

  protected:
    Random _random;
    typedef Distance<S, T, Random> D;
    bool _verbose;
 
    
    int rc; //for macro processisng
    
  protected:
    MDB_env* _env;
    MDB_dbi _dbi_raw;
    MDB_dbi _dbi_tree;
    MDB_txn* _txn;
  
    int _f ; // the dimension of data
    int _tree_count; //number of trees;
    int _K ; // maximum size of data in each leaf node

 public:

    ~AnnoyIndex()    {
    }
    AnnoyIndex(int f, int K, int r) : _random() {
      _f = f;
      _tree_count = r;
      _K = K;
      _verbose = false;

      _env = NULL;
      _txn = NULL;
      _set_roots();
    }
    
    
    bool init_read(const char* database_directory, int maxreaders) {
      int rc;
      close_db();
      E(mdb_env_create(&_env));
      E(mdb_env_set_maxreaders(_env, maxreaders));
      E(mdb_env_set_maxdbs(_env, 100));
      E(mdb_env_open(_env, database_directory, MDB_RDONLY, 0664));
      return true;
    }
    
    
    bool init_write(const char* database_directory, int maxreaders, uint64_t maxsize) {
      int rc;
      close_db();
      E(mdb_env_create(&_env));
      E(mdb_env_set_maxreaders(_env, maxreaders));
      E(mdb_env_set_mapsize(_env, maxsize));
      E(mdb_env_set_maxdbs(_env, 100));
      E(mdb_env_open(_env, database_directory, MDB_WRITEMAP, 0664));
     
      return true;
    }
    
    bool close_db() {
      if (_env != NULL) {
          mdb_env_close(_env);
          _env = NULL;
      }
      return true;
    }
 

  

    void set_verbose(bool v) {
        _verbose = v;
    }
    
    //append data into this tree
  
    void add_item(S item, const T* w){
      return;
    }
  
    void build(int q) {
      return;
    }
    bool save(const char* filename) { return true; }
    void reinitialize() {
      return;
    }
  void unload() {
    return;
  }
  bool load(const char* filename)  {
    return true;
  }
  T get_distance(S i, S j) {
    return (T) 0.0;
  }
  void get_nns_by_item(S item, size_t n, size_t search_k, vector<S>* result, vector<T>* distances) {
    return;
  }
  void get_nns_by_vector(const T* w, size_t n, size_t search_k, vector<S>* result, vector<T>* distances) {
    return ;
  }
  S get_n_items() {
    return 0;
  }
  void verbose(bool v){
    set_verbose(v);
  }
  void get_item(S item, vector<T>* v)  {
    return;
  };


    void add_item(int data_id, data_info& d) {
    
        _add_raw_data(data_id, d);
        
        for (int i = 0; i < _tree_count; i ++ ) {
            _add_item_to_tree(i, data_id);
        }
        return;
    }
  
    void _add_item_to_tree(int node_index, int data_id) {
      //check node type
      tree_node tn = _get_node_by_index(node_index);
      if (tn.leaf() == true) {
          
        //check if it needs to split
        int size = tn.items_size() ;
        if (size + 1 <= _K) {
          tn.add_items(data_id);
          _update_tree_node(node_index, tn);
        } else {
            //split
          tree_node new_node;
          tree_node left_node;
          tree_node right_node;
          D::split(tn, new_node, left_node, right_node, random, _f );
          _update_tree_node(node_index, new_node);
          _add_node(left_node);
          _add_node(right_node);
          
        }
      }
        
    }
  
    vector<int> get_roots() {
        
        vector<int> roots;
        
 
        MDB_val key, data;
        MDB_txn *txn;
        MDB_cursor *cursor;
        MDB_dbi dbi;
        
        E(mdb_txn_begin(_env, NULL, MDB_RDONLY, &txn));
        E(mdb_dbi_open(txn, DBN_ROOT, 0, &dbi));
        E(mdb_cursor_open(txn, dbi, &cursor));
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        
        roots.push_back(atoi((char*)data.mv_data));
        
        //printf("%s\n", mdb_strerror(rc));
        if (rc == 0) {
            while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
                roots.push_back(atoi((char*)data.mv_data));
                
            }
        }
        CHECK(rc == MDB_NOTFOUND, "mdb_cursor_get");
        mdb_dbi_close(_env, dbi);
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        
        return roots;
        
    }
    

    
    //not protected by txn, only use it when you know what you are doing 
  protected:
  
    
    bool _set_roots() {
      
      MDB_val key, data;
      MDB_txn *txn;
      MDB_dbi dbi;
      bool success = true;
      
      
      E(mdb_txn_begin(_env, NULL, 0 , &txn));
      E(mdb_dbi_open(txn, DBN_ROOT, MDB_CREATE, &dbi));
      
      
      for (int i = 0; i < _tree_count; i ++) {
        key.mv_data = (uint8_t*) & i;
        key.mv_size = sizeof(int);
        char tmp[20];
        sprintf(tmp, "rt_%d", i);
        data.mv_data = tmp;
        data.mv_size = strlen(tmp);
        int retval = mdb_put(_txn, dbi, &key, &data, 0);
        if (retval != MDB_SUCCESS) {
            printf("failed add root for tree %d, due to %s \n" , i, mdb_strerror(retval));
            success = false;
        }
      }
      
      if (success) {
        MDB_dbi dbi_tree;
        E(mdb_dbi_open(txn, DBN_TREE, MDB_CREATE, &dbi_tree));
        for (int i = 0; i < _tree_count; i ++) {
          key.mv_data = (uint8_t*) & i;
          key.mv_size = sizeof(int);
          tree_node tn;
          tn.set_index(i);
          tn.set_leaf(true);
          string str;
          tn.SerializeToString(&str);
          data.mv_data = (uint8_t*)str.c_str();
          data.mv_size = str.length();
          int retval = mdb_put(_txn, dbi, &key, &data, 0);
          if (retval != MDB_SUCCESS) {
              printf("failed add root for tree %d, due to %s \n" , i, mdb_strerror(retval));
              success = false;
              break;
          }
        }
      }
      
      if (success) {
        E(mdb_txn_commit(_txn));
      } else {
        mdb_txn_abort(txn);
      }
      return success;
    }
  
  
  int _get_max_tree_index() {
    
    
  }
    
    
    bool _add_node(tree_node & tn) {
        
        //get the largest index
        int max_index = _get_max_tree_index();
        return _update_tree_node(max_index + 1, tn);
    }
    
    bool _update_tree_node(int index, tree_node & tn) {
        int success = 0;
        MDB_val key, data;
        
        key.mv_data = (uint8_t*) & index;
        key.mv_size = sizeof(int);
        
        string data_buffer;
        tn.SerializeToString(&data_buffer);
        
        data.mv_size = data_buffer.length();
        data.mv_data = (uint8_t*)data_buffer.c_str();
        
        int retval = mdb_put(_txn, _dbi_tree, &key, &data, 0);
        
        
        if (retval == MDB_SUCCESS) {
            /*
             printf("successful to put %d, %d : key: %p %.*s, data: %p %.*s, due to : %s\n",
             (int) key.mv_size, (int) data.mv_size, key.mv_data,  (int) key.mv_size,  (char *) key.mv_data,
             data.mv_data, (int) data.mv_size, (char *) data.mv_data, mdb_strerror(retval));
             */
            success = 1;
        }
        else if (retval == MDB_KEYEXIST) {
            printf(" key/data pair is duplicated.\n");
            success = 0;
        } else {
            printf("failed to put %d, %d : key: %p %.*s, data: %p %.*s, due to : %s\n",
                   (int) key.mv_size, (int) data.mv_size, key.mv_data,  (int) key.mv_size,  (char *) key.mv_data,
                   data.mv_data, (int) data.mv_size, (char *) data.mv_data, mdb_strerror(retval));
            
            success = 0;
        }
        
        return success ;

    }
    
    bool _get_node_by_index(int index,  tree_node & tn ) {
        
        MDB_val key, data;
        key.mv_data = (uint8_t*) & index;
        key.mv_size = sizeof(int);
        rc = mdb_get(_txn, _dbi_tree, &key, &data);
        if (rc == 0) {
            string s_data((char*) data.mv_data, data.mv_size);
            tn.ParseFromString(s_data);
        } else {
            //printf("can not find raw image data with id: %d\n", image_id);
            return false;
        }
        return true;
        
    }

    
    bool _get_raw_data(int data_id,  data_info & rdata ) {
 
        MDB_val key, data;
        key.mv_data = (uint8_t*) & data_id;
        key.mv_size = sizeof(int);
        rc = mdb_get(_txn, _dbi_raw, &key, &data);
        if (rc == 0) {
            string s_data((char*) data.mv_data, data.mv_size);
            rdata.ParseFromString(s_data);
        } else {
            //printf("can not find raw image data with id: %d\n", image_id);
            return false;
        }
        return true;
        
    }
    
    
    int _add_raw_data(int data_id, data_info& rdata) {
        
        int success = 0;
        MDB_val key, data;
        
        key.mv_data = (uint8_t*) & data_id;
        key.mv_size = sizeof(int);
        
        string data_buffer;
        rdata.SerializeToString(&data_buffer);
        
        data.mv_size = data_buffer.length();
        data.mv_data = (uint8_t*)data_buffer.c_str();
        
        int retval = mdb_put(_txn, _dbi_raw, &key, &data, 0);
        
        
        if (retval == MDB_SUCCESS) {
            /*
             printf("successful to put %d, %d : key: %p %.*s, data: %p %.*s, due to : %s\n",
             (int) key.mv_size, (int) data.mv_size, key.mv_data,  (int) key.mv_size,  (char *) key.mv_data,
             data.mv_data, (int) data.mv_size, (char *) data.mv_data, mdb_strerror(retval));
             */
            success = 1;
        }
        else if (retval == MDB_KEYEXIST) {
            printf(" key/data pair is duplicated.\n");
            success = 0;
        } else {
            printf("failed to put %d, %d : key: %p %.*s, data: %p %.*s, due to : %s\n",
                   (int) key.mv_size, (int) data.mv_size, key.mv_data,  (int) key.mv_size,  (char *) key.mv_data,
                   data.mv_data, (int) data.mv_size, (char *) data.mv_data, mdb_strerror(retval));
            
            success = 0;
        }
        
        return success ;
        
    }
    
    
};



#endif
