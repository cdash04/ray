/*
 	Ray
    Copyright (C) 2010, 2011, 2012 Sébastien Boisvert

	http://DeNovoAssembler.SourceForge.Net/

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You have received a copy of the GNU General Public License
    along with this program (gpl-3.0.txt).  
	see <http://www.gnu.org/licenses/>
*/

#include "GenomeToTaxonLoader.h"
#include "types.h"

#include <assert.h>
#include <iostream>
using namespace std;

void GenomeToTaxonLoader::load(string file){

	STEPPING=1000000;

	m_current=0;
	m_size=0;

	m_stream.open(file.c_str());
	
	if(!m_stream){
		cout<<"File "<<file<<" is invalid."<<endl;

		m_stream.close();

	}
	
	int count=0;
	//while loop to count the number of line in the Genome-To-Taxon file
	while(!m_stream.eof()){
		string a="";

		m_stream>>a;

		if(a!=""){
			count++;

			if(count==2){
				count=0;
				m_size++;

				if(m_size % STEPPING == 0){
					cout<<"GenomeToTaxonLoader::load "<<m_size<<endl;
				}
			}
		}

	}
	//buffer stream is empty, closing it
	m_stream.close();
	//reopen buffer to get all Genome-To-Taxon lines again
	m_stream.open(file.c_str());

	cout<<"File "<<file<<" has "<<m_size<<" entries"<<endl;
}

bool GenomeToTaxonLoader::hasNext(){
	return m_current<m_size;
}

void GenomeToTaxonLoader::getNext(GenomeIdentifier*genome,TaxonIdentifier*taxon){

	if(m_current % STEPPING == 0){
		cout<<"GenomeToTaxonLoader::getNext "<<m_current<<"/"<<m_size<<endl;
	}
	string tmpGenome;
	GenomeIdentifier loadedGenome=0;
	TaxonIdentifier loadedTaxon;

	m_stream>>tmpGenome>>loadedTaxon;
	std::cout << "the ifstream contain : " << tmpGenome << std::endl;

    for (string::const_iterator it = tmpGenome.begin();it!= tmpGenome.end();++it){
        loadedGenome = ((int) *it) + (loadedGenome << 6)  + (loadedGenome << 16) - loadedGenome ;
    }
    loadedGenome /= 10000000000;

	std::cout << "genome " << loadedGenome << " linked to taxon " << loadedTaxon << std::endl;
	(*genome)=loadedGenome;
	(*taxon)=loadedTaxon;

	m_current++;

	if(m_current== m_size){
		m_stream.close();
	}
}

