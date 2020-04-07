#include <fstream.h>
#include <ctype.h>
#include "parse.h"
#include "getvalue.h"
#include "version.h"
#include "stringo.h"
#include "findsym.h"

void find_all_atom_in_subcell(Array<rVector3d> *psub_atom_pos,
				Array<int> *psub_atom_type,
				const Array<rVector3d> &atom_pos,
				const Array<int> &atom_type,
				const rMatrix3d &cell, const rMatrix3d &subcell) {
  int nb_atom_subcell=iround((Real)atom_pos.get_size()*fabs(det(subcell)/det(cell)));
  rMatrix3d isubcell=(!subcell);
  psub_atom_pos->resize(nb_atom_subcell);
  psub_atom_type->resize(nb_atom_subcell);
  int redun=atom_pos.get_size()/nb_atom_subcell;
  Array<int> done(atom_pos.get_size());
  zero_array(&done);
  int j=0;
  for (int i=0; i<nb_atom_subcell; i++) {
    while (done(j)==1) {j++;}
    (*psub_atom_pos)(i)=atom_pos(j);
    (*psub_atom_type)(i)=atom_type(j);
    for (int r=0; r<redun; r++) {
      Real mind=MAXFLOAT;
      int mins=0;
      for (int s=0; s<atom_pos.get_size(); s++) {
	if (done(s)!=1) {
	  Real d=cylinder_norm(isubcell*(atom_pos(s)-atom_pos(j)));
	  if (d<mind) {
	    mind=d;
	    mins=s;
	  }
	}
      }
      done(mins)=1;
    }
  }
  wrap_inside_cell(psub_atom_pos,*psub_atom_pos,subcell);
}

/*
void find_all_atom_in_subcell(Array<rVector3d> *psuper_atom_pos,
				Array<int> *psuper_atom_type,
				const Array<rVector3d> &atom_pos,
				const Array<int> &atom_type,
				const rMatrix3d &cell, const rMatrix3d &subcell) {
  int nb_atom_subcell=iround(atom_pos.get_size()*fabs(det(subcell)/det(cell)));
  rMatrix3d isubcell=(!subcell);
  psuper_atom_pos->resize(nb_atom_subcell);
  psuper_atom_type->resize(nb_atom_subcell);
  LatticePointInCellIterator l(cell,subcell);
  int index=0;
  for ( ; l; l++) {
    for (int s=0; s<atom_pos.get_size(); s++) {
      int i=0;
      for (; i<index; i++) {
	if (equivalent_mod_cell(l+atom_pos(s),(*psuper_atom_pos)(i),isubcell)) break;
      }
      if (i==index) {
	(*psuper_atom_pos)(index)=l+atom_pos(s);
	(*psuper_atom_type)(index)=atom_type(s);
	index++;
      }
    }
  }
  wrap_inside_cell(psuper_atom_pos,*psuper_atom_pos,subcell);
}
*/

void read_axes_cell(rMatrix3d *pcell, istream &file) {
  rMatrix3d axes;
  read_cell(&axes,file);
  for (int i=0; i<3; i++) {
    rVector3d v;
    file >> v;
    pcell->set_column(i,axes*v);
  }
}

void add_redundant_atom_in_cell(Array<rVector3d> *patom_pos,
				Array<int> *patom_type,
				const Array<rVector3d> &atom_pos,
				const Array<int> &atom_type,
				const rMatrix3d &cell) {
  rMatrix3d icell=!cell;
  Array<rVector3d> fatom_pos(atom_pos.get_size());
  for (int s=0; s<atom_pos.get_size(); s++) {
    fatom_pos(s)=mod1(icell*atom_pos(s));
  }
  LinkedList<rVector3d> atom_pos_list;
  LinkedList<int> atom_type_list;
  MultiDimIterator<iVector3d> l(iVector3d(-1,-1,-1),iVector3d(1,1,1));
  for (; l; l++) {
    for (int s=0; s<fatom_pos.get_size(); s++) {
      rVector3d v=to_real(l)+fatom_pos(s);
      if (in01included(v)) {
	atom_pos_list << new rVector3d(cell*v);
	atom_type_list << new int(atom_type(s));
      }
    }
  }
  LinkedList_to_Array(patom_pos,atom_pos_list);
  LinkedList_to_Array(patom_type,atom_type_list);
}

rVector3d rnd_ball(void) {
  rVector3d v;
  do {
    for (int i=0; i<3; i++) {v(i)=2.*uniform01()-1.;}
  } while (norm2(v)>1.);
  return v;
}

int main(int argc, char *argv[]) {
  char *axesfilename="";
  char *cellfilename="";
  int simplesupercell=0;
  int docart=0;
  int dofrac=0;
  int doconv=0;
  int doabc=0;
  int donoabc=0;
  int readocc=0;
  int sigdig=6;
  int fixcell=0;
  int find_t=0;
  char *orig_setting="";
  char *final_setting="";
  int dorecip=0;
  int nbskip=1;
  int nbprint=MAXINT;
  int printnb=0;
  int printvol=0;
  int dosym=0;
  int dosymname=0;
  int printbravaistype=0;
  char *shift_str="";
  rVector3d shift(0.,0.,0.);
  int wrapinside=0;
  int wrapinsideunshift=0;
  char *spcgrpfile="";
  char *strtokeep="";
  int genspcgrp=0;
  int remred=0;
  int remredavg=0;
  int addred=0;
  Real scale=1.;
  int seed=0;
  Real jitteratom=0.;
  Real jittercell=0.;
  AskStruct options[]={
    {"","cellcvrt " MAPS_VERSION ", by Axel van de Walle",TITLEVAL,NULL},
    {"","Performs various conversions/changes on structure (or lattices).",TITLEVAL,NULL},
    {"","Reads from stdin, writes to stdout.",TITLEVAL,NULL},
    {"-c","Use cartesian coordinates",BOOLVAL,&docart},
    {"-f","Use fractional coordinates",BOOLVAL,&dofrac},
    {"-cc","Find conventional cell and use it as coordinate system",BOOLVAL,&doconv},
    {"-abc","Use a b c alpha beta gamma format to specify axes",BOOLVAL,&doabc},
    {"-noabc","Use cartesian format to specify the axes (the default)",BOOLVAL,&donoabc},
    {"-ro","Read fractional Occupation",BOOLVAL,&readocc},
    {"-u","User-specified coordinate system input file (optional)",STRINGVAL,&axesfilename},
    {"-uss","User-specified supercell",STRINGVAL,&cellfilename},
    {"-ss","Simple supercell (multiple of axes in all directions)",INTVAL,&simplesupercell},
    {"-fc","Fix cell (to make it as symmetric as possible)",BOOLVAL,&fixcell},
    {"-s","Look for smaller unit cell",BOOLVAL,&find_t},
    {"-sh","Shift all atoms (default 0,0,0).",STRINGVAL,&shift_str},
    {"-ja","Jitter all atoms positions by a random amount less than specified",REALVAL,&jitteratom},
    {"-jc","Jitter all cell parameters by a random amount less than the fraction specified",REALVAL,&jittercell},
    {"-sd","Seed for random number generation (default: use clock)",INTVAL,&seed},
    {"-sg","Space group file",STRINGVAL,&spcgrpfile},
    {"-gsg","Generate Space Group",BOOLVAL,&genspcgrp},
    {"-wi","Wrap all atoms inside unit cell",BOOLVAL,&wrapinside},
    {"-wiu","Wrap all atoms inside unit cell and undo shift",BOOLVAL,&wrapinsideunshift},
    {"-rr","Remove redundant atoms",BOOLVAL,&remred},
    {"-rra","Remove redundant atoms and average",BOOLVAL,&remredavg},
    {"-ar","Add redundant atoms",BOOLVAL,&addred},
    {"-osf","Original setting file (optional)",STRINGVAL,&orig_setting},
    {"-fsf","Final setting file (optional)",STRINGVAL,&final_setting},
    {"-r","Print reciprocal unit cell",BOOLVAL,&dorecip},
    {"-fs","Index of first structure to process (default: 1)",INTVAL,&nbskip},
    {"-ns","Number of structures to process",INTVAL,&nbprint},
    {"-slf","Structure list file",STRINGVAL,&strtokeep},
    {"-pn","Print the number of atoms in the structure only",BOOLVAL,&printnb},
    {"-pv","Print volume of cell only",BOOLVAL,&printvol},
    {"-sym","Print spacegroup",BOOLVAL,&dosym},
    {"-ppg","Print point group name",BOOLVAL,&dosymname},
    {"-pbv","Print Bravais Lattice name",BOOLVAL,&printbravaistype},
    {"-sc","Scale factor (default: 1)",REALVAL,&scale},
    {"-sig","Number of significant digits to print in output files",INTVAL,&sigdig},
    {"-z","Tolerance for checking overlap (Default 1e-3)",REALVAL,&zero_tolerance},
  };
  if (!get_values(argc,argv,countof(options),options)) {
    display_help(countof(options),options);
    return 1;
  }
  if (docart+dofrac+doconv+(strlen(axesfilename)>0)>1) {
    ERRORQUIT("Must specify no more than one of -c, -f, -cc or -u=filename.");
  }
  rMatrix3d transfo;
  transfo.identity();
  if (strlen(orig_setting)>0 || strlen(final_setting)>0) {
    rMatrix3d orig;
    orig.identity();
    if (strlen(orig_setting)>0) {
      ifstream origfile(orig_setting);
      if (!origfile) {ERRORQUIT("Unable to open original setting file");}
      read_axes_cell(&orig,origfile);
    }
    rMatrix3d final;
    final.identity();
    if (strlen(final_setting)>0) {
      ifstream finalfile(final_setting);
      if (!finalfile) {ERRORQUIT("Unable to open final setting file");}
      read_axes_cell(&final,finalfile);
    }
    transfo=final*(!orig);
  }

  // read in structure list if any;
  LinkedList<int> strlist;
  if (strlen(strtokeep)>0) {
    nbskip=1;
    ifstream file(strtokeep);
    if (!file) ERRORQUIT("Unable to open structure list file.");
    while (1) {
      int s=-1;
      file >> s;
      if (s==-1) break;
      strlist << new int(s);
    }
  }
  LinkedListIterator<int> istr(strlist);

  // read in lattice (see parse.h);
  Structure lat;
  Array<Arrayint> site_type_list;
  Array<AutoString> atom_label;
  Array<Array<Real> > occ;
  rMatrix3d axes;
  for (int s=1; s<nbskip; s++) {
    if (readocc) {
      parse_rndstr_file(&lat.cell, &lat.atom_pos, &lat.atom_type, &occ, &site_type_list, &atom_label, cin, &axes);
    }
    else {
      parse_lattice_file(&lat.cell, &lat.atom_pos, &lat.atom_type, &site_type_list, &atom_label, cin, &axes);
    }
    skip_to_next_structure(cin);
  }
  int printend=0;
  for (int s=0; s<nbprint && nbprint!=0; s++) {
    if (cin.eof()) break;

    if (readocc) {
      parse_rndstr_file(&lat.cell, &lat.atom_pos, &lat.atom_type, &occ, &site_type_list, &atom_label, cin, &axes);
    }
    else {
      parse_lattice_file(&lat.cell, &lat.atom_pos, &lat.atom_type, &site_type_list, &atom_label, cin, &axes);
    }
    skip_to_next_structure(cin);

    if (strlen(strtokeep)>0) {
      if (!istr) break;
      if ((s+1)!=*istr) continue;
      istr++;
    }

    if (printend) {
      cout << "end" << endl << endl;
    }
    else {
      printend=1;
    }
    
    rndseed(seed);
    if (jitteratom>0.) {
      for (int at=0; at<lat.atom_pos.get_size(); at++) {
	lat.atom_pos(at)+=rnd_ball()*jitteratom;
      }
    }
    if (jittercell>0.) {
      for (int i=0; i<3; i++) {
	rVector3d v=rnd_ball()*jittercell;
	for (int j=0; j<3; j++) {
	  transfo(j,i)+=v(j);
	}
      }
    }
    lat.cell=transfo*lat.cell;
    axes=transfo*axes;
    for (int i=0; i<lat.atom_pos.get_size(); i++) {
      lat.atom_pos(i)=transfo*lat.atom_pos(i);
    }
    if (fixcell) {
      lat.cell=find_symmetric_cell(lat.cell);
    }
    if (find_t) {
      Structure tmp_lat;
      tmp_lat.cell=lat.cell;
      tmp_lat.atom_pos=lat.atom_pos;
      tmp_lat.atom_type=lat.atom_type;
      find_primitive_unit_cell(&(lat.cell),tmp_lat);
      lat.cell=find_symmetric_cell(lat.cell);
      find_all_atom_in_subcell(&lat.atom_pos,&lat.atom_type,tmp_lat.atom_pos,tmp_lat.atom_type,tmp_lat.cell,lat.cell);
    }
    if (strlen(cellfilename)>0 || simplesupercell>0) {
      Structure supercell;
      if (simplesupercell>0) {
	supercell.cell.identity();
      }
      else {
	ifstream cellfile(cellfilename);
	if (!cellfile) ERRORQUIT("Unable to open cell file.");
	read_cell(&supercell.cell,cellfile);
      }
      supercell.cell=(axes*supercell.cell)*(simplesupercell>1 ? (Real)simplesupercell : 1.);
      if (!is_int((!lat.cell)*supercell.cell)) {
	  ERRORQUIT("Supercell is incomensurate with unit cell");
      }
      find_all_atom_in_supercell(&supercell.atom_pos,&supercell.atom_type,lat.atom_pos,lat.atom_type,lat.cell,supercell.cell);
      lat=supercell;
    }
    for (int at=0; at<lat.atom_pos.get_size(); at++) {
      lat.atom_pos(at)=lat.atom_pos(at)*scale;
    }
    lat.cell=lat.cell*scale;
    axes=axes*scale;

    if (docart) {
      axes.identity();
    }
    else if (dofrac) {
      axes=lat.cell;
    }
    else if (doconv) {
      AutoString bts;
      print_bravais_type(&bts,find_bravais_type(&axes,find_reduced_cell(lat.cell)));
    }
    else if (strlen(axesfilename)>0) {
      ifstream file(axesfilename);
      if (!file) {
	ERRORQUIT("Unable to open user-defined axes input file.");
      }
      read_cell(&axes,file);
    }
    if (strlen(shift_str)>0) {
      istrstream s(shift_str);
      for (int i=0; i<3; i++) {
	skip_delim(s,",;:");
	s >> shift(i);
      }
      shift=axes*shift;
      for (int i=0; i<lat.atom_pos.get_size(); i++) {
	lat.atom_pos(i)+=shift;
      }
    }
    if (strlen(spcgrpfile)>0) {
      ifstream file(spcgrpfile);
      if (!file) ERRORQUIT("Unable to open space group file.");
      int nop=0;
      file >> nop;
      SpaceGroup spacegroup;
      spacegroup.point_op.resize(nop);
      spacegroup.trans.resize(nop);
      spacegroup.cell=lat.cell;
      for (int op=0; op<nop; op++) {
	rMatrix3d m;
	file >> m;
	spacegroup.point_op(op)=axes*(m*(!axes));
	rVector3d v;
	file >> v;
	spacegroup.trans(op)=axes*v;
      }
      if (genspcgrp) {
	generate_space_group(&spacegroup, spacegroup);

	ofstream symfile("fullsym.out");
	symfile.setf(ios::fixed);
	symfile.precision(sigdig);
	symfile << spacegroup.point_op.get_size() << endl;
	for (int i=0; i<spacegroup.point_op.get_size(); i++) {
	  symfile << ((!axes)*(spacegroup.point_op(i))*axes) << endl;
	  symfile << ((!axes)*(spacegroup.trans(i))) << endl;
	  symfile << endl;
	}
      }
      Structure tmp;
      tmp.cell=lat.cell;
      tmp.atom_pos.resize(lat.atom_pos.get_size()*nop);
      tmp.atom_type.resize(lat.atom_pos.get_size()*nop);
      int at2=0;
      for (int at=0; at<lat.atom_pos.get_size(); at++) {
	for (int op=0; op<nop; op++) {
	  tmp.atom_pos(at2)=spacegroup.point_op(op)*lat.atom_pos(at)+spacegroup.trans(op);
	  tmp.atom_type(at2)=lat.atom_type(at);
	  at2++;
	}
      }
      lat=tmp;
      if (!remredavg) remred=1;
    }
    if (remred) {
      rMatrix3d icell=!(lat.cell); 
      LinkedList<rVector3d> newpos;
      LinkedList<int> newtype;
      for (int i=0; i<lat.atom_pos.get_size(); i++) {
	if (which_atom(lat.atom_pos,lat.atom_pos(i),icell)==i) {
	  newpos << new rVector3d(lat.atom_pos(i));
	  newtype << new int(lat.atom_type(i));
	}
      }
      LinkedList_to_Array(&lat.atom_pos,newpos);
      LinkedList_to_Array(&lat.atom_type,newtype);
    }
    if (remredavg) {
      rMatrix3d icell=!(lat.cell); 
      Array<rVector3d> avgpos(lat.atom_pos.get_size());
      Array<Real> weight(lat.atom_pos.get_size());
      zero_array(&weight);
      for (int i=0; i<avgpos.get_size(); i++) {avgpos(i)=rVector3d(0.0,0.0,0.0);}
      for (int i=0; i<lat.atom_pos.get_size(); i++) {
	int at=which_atom(lat.atom_pos,lat.atom_pos(i),icell);
	rVector3d d=icell*(lat.atom_pos(i)-lat.atom_pos(at));
	rVector3d sh=lat.cell*to_real(to_int(d));
	avgpos(at)+=lat.atom_pos(i)-sh;
	weight(at)+=1.;
      }
      LinkedList<rVector3d> newpos;
      LinkedList<int> newtype;
      for (int i=0; i<lat.atom_pos.get_size(); i++) {
	if (weight(i)>0) {
	  newpos << new rVector3d(avgpos(i)/weight(i));
	  newtype << new int(lat.atom_type(i));
	}
      }
      LinkedList_to_Array(&lat.atom_pos,newpos);
      LinkedList_to_Array(&lat.atom_type,newtype);
    }
    if (wrapinside || wrapinsideunshift) {
      wrap_inside_cell(&lat.atom_pos,lat.atom_pos,lat.cell);
    }
    if (wrapinsideunshift) {
      for (int i=0; i<lat.atom_pos.get_size(); i++) {
	lat.atom_pos(i)-=shift;
      }
    }
    if (addred) {
      add_redundant_atom_in_cell(&lat.atom_pos,&lat.atom_type,lat.atom_pos,lat.atom_type,lat.cell);
    }

    cout.setf(ios::fixed);
    cout.precision(sigdig);
    if (printnb) {
      cout << lat.atom_pos.get_size() << endl;
    }
    else if (printvol) {
      cout << det(lat.cell) << endl;
    }
    else if (dorecip) {
      rMatrix3d recip=!~lat.cell;
      for (int i=0; i<3; i++) {
	cout << recip.get_column(i) << endl;
      }
    }
    else if (dosym || dosymname) {
      SpaceGroup spacegroup;
      spacegroup.cell=lat.cell;
      find_spacegroup(&spacegroup.point_op,&spacegroup.trans,lat.cell,lat.atom_pos,lat.atom_type);
      if (contains_pure_translations(spacegroup.point_op,spacegroup.trans)) {
	cerr << "Warning: unit cell is not primitive." << endl;
      }
      if (dosymname) {
	PointGroupType pg;
	pg=find_point_group_type(spacegroup.point_op);
	cout << point_group_name(pg) << endl;
      }
      else {
	cout << spacegroup.point_op.get_size() << endl;
	for (int i=0; i<spacegroup.point_op.get_size(); i++) {
	  cout << ((!axes)*(spacegroup.point_op(i))*axes) << endl;
	  cout << ((!axes)*(spacegroup.trans(i))) << endl;
	  cout << endl;
	}
      }
    }
    else if (printbravaistype) {
      AutoString bts;
      rMatrix3d conv;
      print_bravais_type(&bts,find_bravais_type(&conv,find_reduced_cell(lat.cell)));
      cout << bts << endl;
      cout << "Conventional cell in cartesian:" << endl;
      for (int i=0; i<3; i++) {
	cout << conv.get_column(i) << endl;
      }
      conv=(!axes)*conv;
      cout << "Conventional cell in the given coordinate system:" << endl;
      for (int i=0; i<3; i++) {
	cout << conv.get_column(i) << endl;
      }
    }
    else {
      write_lattice(lat,site_type_list,atom_label,axes,cout,doabc);
    }
  }
}
