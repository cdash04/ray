/*
 	Ray
    Copyright (C)  2010  Sébastien Boisvert

	http://DeNovoAssembler.SourceForge.Net/

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You have received a copy of the GNU General Public License
    along with this program (COPYING).  
	see <http://www.gnu.org/licenses/>

*/


#ifndef _SplayTreeIterator
#define _SplayTreeIterator

#include<MyStack.h>
#include<SplayNode.h>
#include<SplayTree.h>
#include<stdlib.h>

// based on
// http://www.liafa.jussieu.fr/~carton/Enseignement/Algorithmique/LicenceMathInfo/Programmation/Tree/parcours.html
// http://www.liafa.jussieu.fr/~carton/Enseignement/Algorithmique/Programmation/Tree/Sources/Tree.java
// iterator infixe (from the lowest to the greatest.)

template<class AVL_KEY,class AVL_VALUE>
class SplayTreeIterator{
	MyStack<SplayNode<AVL_KEY,AVL_VALUE>*>m_stack;
public:
	SplayTreeIterator(SplayTree<AVL_KEY,AVL_VALUE>*tree);
	void constructor(SplayTree<AVL_KEY,AVL_VALUE>*tree);
	SplayTreeIterator();
	bool hasNext();
	SplayNode<AVL_KEY,AVL_VALUE>*next();
};


template<class AVL_KEY,class AVL_VALUE>
SplayTreeIterator<AVL_KEY,AVL_VALUE>::SplayTreeIterator(){
}

template<class AVL_KEY,class AVL_VALUE>
SplayTreeIterator<AVL_KEY,AVL_VALUE>::SplayTreeIterator(SplayTree<AVL_KEY,AVL_VALUE>*tree){
	constructor(tree);
}

template<class AVL_KEY,class AVL_VALUE>
void SplayTreeIterator<AVL_KEY,AVL_VALUE>::constructor(SplayTree<AVL_KEY,AVL_VALUE>*tree){
	if(tree->getRoot()!=NULL){
		m_stack.push(tree->getRoot());
	}
}

template<class AVL_KEY,class AVL_VALUE>
bool SplayTreeIterator<AVL_KEY,AVL_VALUE>::hasNext(){
	return m_stack.size()>0;
}

template<class AVL_KEY,class AVL_VALUE>
SplayNode<AVL_KEY,AVL_VALUE>*SplayTreeIterator<AVL_KEY,AVL_VALUE>::next(){
	if(hasNext()){
		SplayNode<AVL_KEY,AVL_VALUE>*c=m_stack.top();
		m_stack.pop();
		if(c->getLeft()!=NULL){
			m_stack.push(c->getLeft());
		}
		if(c->getRight()!=NULL){
			m_stack.push(c->getRight());
		}
		return c;
	}else{
		return NULL;
	}
}



#endif
