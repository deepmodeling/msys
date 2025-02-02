#include "../amber.hxx"
#include "../types.hxx"
#include "../schema.hxx"
#include "../clone.hxx"
#include "../elements.hxx"
#include "../analyze.hxx"
#include "../term_table.hxx"

#include <fstream>
#include <cstring>
#include <cmath>
#include <iomanip>

using namespace desres::msys;

namespace {

    std::string parse_flag(std::string const& line) {
        std::string flag = line.substr(5);
        trim(flag);
        return flag;
    }

    struct Format {
        int nperline;
        int width;
        char type;

        Format() : nperline(), width(), type() {}
        Format(std::string const& line) {
            size_t lp = line.find('(', 7);
            size_t rp = line.find(')', lp);
            if (lp==std::string::npos || rp==std::string::npos) {
                MSYS_FAIL("Expected %FORMAT(fmt), got '" << line << "'");
            }
            if (sscanf(line.c_str()+lp+1, "%d%c%d",
                       &nperline, &type, &width)!=3) {
                MSYS_FAIL("Error parsing FORMAT '" << line << "'");
            }
        }
    };

    enum Pointers {
        Natom, Ntypes, Nbonh, Nbona, Ntheth,
        Ntheta,Nphih,  Nphia, Jparm, Nparm,
        Nnb,   Nres,   Mbona, Mtheta,Mphia,
        Numbnd,Numang, Nptra, Natyp, Nphb,
        Ifpert,Nbper,  Ngper, Ndper, Mbper,
        Mgper, Mdper,  IfBox, Nmxrs, IfCap,
        NUM_POINTERS
    };

    struct Section {
        String flag;
        Format fmt;
        String data;
    };
    typedef std::map<std::string, Section> SectionMap;

    void parse_ints(SectionMap const& map, String const& name, std::vector<int>& v)
    {
        SectionMap::const_iterator it=map.find(name);
        if (it==map.end()) MSYS_FAIL("Missing section " << name);
        Section const& sec = it->second;
        for (unsigned i=0; i<v.size(); i++) {
            std::string elem = sec.data.substr(i*sec.fmt.width, sec.fmt.width);
            if (sscanf(elem.c_str(), "%d", &v[i])!=1) {
                MSYS_FAIL("Parsing ints for " << sec.flag);
            }
        }
    }

    void parse_strs(SectionMap const& map, String const& name,
                    std::vector<String>& v)
    {
        SectionMap::const_iterator it=map.find(name);
        if (it==map.end()) MSYS_FAIL("Missing section " << name);
        Section const& sec = it->second;
        for (unsigned i=0; i<v.size(); i++) {
            v[i] = sec.data.substr(i*sec.fmt.width, sec.fmt.width);
            trim(v[i]);
        }
    }

    void parse_flts(SectionMap const& map, String const& name, std::vector<double>& v)
    {
        SectionMap::const_iterator it=map.find(name);
        if (it==map.end()) MSYS_FAIL("Missing section " << name);
        Section const& sec = it->second;
        for (unsigned i=0; i<v.size(); i++) {
            std::string elem = sec.data.substr(i*sec.fmt.width, sec.fmt.width);
            if (sscanf(elem.c_str(), "%lf", &v[i])!=1) {
                MSYS_FAIL("Parsing dbls for " << sec.flag);
            }
        }
    }

}

namespace {
    struct Pair {
        Id ai, aj;
        double es;
        double lj;

        Pair() : ai(BadId), aj(BadId), es(), lj() {}
        Pair(Id i, Id j, double e, double l)
        : ai(i), aj(j), es(e), lj(l) {}
    };
    typedef std::vector<Pair> PairList;
}


static void parse_nonbonded(SystemPtr mol, SectionMap const& map, int ntypes,
                            PairList const& pairs) {

    TermTablePtr nb = AddNonbonded(mol, "vdw_12_6", "arithmetic/geometric");
    TermTablePtr pt = AddTable(mol, "pair_12_6_es");
    /* this might be nice for debugging, but I'll leave it off for now */
    //pt->params()->addProp("scee_scale_factor", FloatType);
    //pt->params()->addProp("scnb_scale_factor", FloatType);

    /* add a type column to store the amber atom type */
    nb->params()->addProp("type", StringType);

    int ntypes2 = (ntypes * (ntypes+1))/2;
    std::vector<int> inds(ntypes*ntypes), types(mol->atomCount());
    std::vector<double> acoef(ntypes2), bcoef(ntypes2);
    std::vector<String> vdwtypes(mol->atomCount());

    parse_ints(map, "ATOM_TYPE_INDEX", types);
    parse_ints(map, "NONBONDED_PARM_INDEX", inds);
    parse_flts(map, "LENNARD_JONES_ACOEF", acoef);
    parse_flts(map, "LENNARD_JONES_BCOEF", bcoef);
    parse_strs(map, "AMBER_ATOM_TYPE", vdwtypes);

    for (Id i=0; i<mol->atomCount(); i++) {
        int atype = types.at(i);
        int ico = inds.at((ntypes * (atype-1)+atype)-1);
        double c12 = acoef.at(ico-1);
        double c6  = bcoef.at(ico-1);
        double sig=0, eps=0;
        if (c12!=0 && c6!=0) {
            sig=pow(c12/c6, 1./6.);
            eps=c6*c6/(4*c12);
        }
        Id param = nb->params()->addParam();
        nb->params()->value(param, "sigma") = sig;
        nb->params()->value(param, "epsilon") = eps;
        nb->params()->value(param, "type") = vdwtypes[i];
        IdList ids(1, i);
        nb->addTerm(ids, param);
    }

    for (PairList::const_iterator it=pairs.begin(); it!=pairs.end(); ++it) {
        Id ai = it->ai;
        Id aj = it->aj;
        double lj = 1.0/it->lj;
        double es = 1.0/it->es;
        int itype = types.at(ai);
        int jtype = types.at(aj);
        int ico = inds.at((ntypes * (itype-1) + jtype)-1);
        double c12 = acoef.at(ico-1);
        double c6  = bcoef.at(ico-1);
        double aij = lj * c12;
        double bij = lj * c6;
        double qij = es*mol->atom(ai).charge*mol->atom(aj).charge;
        Id param = pt->params()->addParam();
        pt->params()->value(param, "aij") = aij;
        pt->params()->value(param, "bij") = bij;
        pt->params()->value(param, "qij") = qij;
        //pt->params()->value(param, "scee_scale_factor") = it->es;
        //pt->params()->value(param, "scnb_scale_factor") = it->lj;
        IdList ids(2);
        ids[0] = ai;
        ids[1] = aj;
        pt->addTerm(ids, param);
    }
}

static void parse_stretch(SystemPtr mol, SectionMap const& map,
                          bool without_tables,
                          int ntypes, int nbonh, int nbona) {

    std::vector<double> r0(ntypes), fc(ntypes);
    std::vector<int> bonh(nbonh*3), bona(nbona*3);

    parse_flts(map, "BOND_EQUIL_VALUE", r0);
    parse_flts(map, "BOND_FORCE_CONSTANT", fc);
    parse_ints(map, "BONDS_INC_HYDROGEN", bonh);
    parse_ints(map, "BONDS_WITHOUT_HYDROGEN", bona);

    TermTablePtr tb;
    if (!without_tables) {
        tb = AddTable(mol, "stretch_harm");
        for (int i=0; i<ntypes; i++) {
            Id param = tb->params()->addParam();
            tb->params()->value(param, "fc") = fc[i];
            tb->params()->value(param, "r0") = r0[i];
        }
    }
    IdList ids(2);
    for (int i=0; i<nbonh; i++) {
        ids[0] = bonh[3*i  ]/3;
        ids[1] = bonh[3*i+1]/3;
        /* no bonds between hydrogen, please.  It's just how Amber
         * expresses constraints.  */
        if (mol->atom(ids[0]).atomic_number==1 &&
            mol->atom(ids[1]).atomic_number==1) {
            continue;
        }
        mol->addBond(ids[0], ids[1]);
        if (without_tables) continue;
        tb->addTerm(ids, bonh[3*i+2]-1);
    }
    for (int i=0; i<nbona; i++) {
        ids[0] = bona[3*i  ]/3;
        ids[1] = bona[3*i+1]/3;
        if (mol->atom(ids[0]).atomic_number==1 &&
            mol->atom(ids[1]).atomic_number==1) {
            /* just to be sure, though we don't expect to find H-H bonds
             * in this section. */
            continue;
        }
        mol->addBond(ids[0], ids[1]);
        if (without_tables) continue;
        tb->addTerm(ids, bona[3*i+2]-1);
    }
}

static void parse_angle(SystemPtr mol, SectionMap const& map,
                          int ntypes, int nbonh, int nbona) {

    std::vector<double> r0(ntypes), fc(ntypes);
    std::vector<int> bonh(nbonh*4), bona(nbona*4);

    parse_flts(map, "ANGLE_EQUIL_VALUE", r0);
    parse_flts(map, "ANGLE_FORCE_CONSTANT", fc);
    parse_ints(map, "ANGLES_INC_HYDROGEN", bonh);
    parse_ints(map, "ANGLES_WITHOUT_HYDROGEN", bona);

    TermTablePtr tb = AddTable(mol, "angle_harm");
    for (int i=0; i<ntypes; i++) {
        Id param = tb->params()->addParam();
        tb->params()->value(param, "fc") = fc[i];
        tb->params()->value(param, "theta0") = r0[i] * 180 / M_PI;
    }
    IdList ids(3);
    for (int i=0; i<nbonh; i++) {
        ids[0] = bonh[4*i  ]/3;
        ids[1] = bonh[4*i+1]/3;
        ids[2] = bonh[4*i+2]/3;
        tb->addTerm(ids, bonh[4*i+3]-1);
    }
    for (int i=0; i<nbona; i++) {
        ids[0] = bona[4*i  ]/3;
        ids[1] = bona[4*i+1]/3;
        ids[2] = bona[4*i+2]/3;
        tb->addTerm(ids, bona[4*i+3]-1);
    }
}


/*
    DESRESCode#3431 Note that while cmaps live in a std::vector and
    are therefore 0-indexed, they are referred to in the
    torsion_torsion_cmap table 1-indexed. Atomids are 0 indexed
*/
static void parse_cmap(SystemPtr mol, SectionMap const& map) {
    std::string prefix = "";

    std::vector<int> counts(2);
    if (map.find("CMAP_COUNT") != map.end()){
        parse_ints(map, "CMAP_COUNT", counts);
    }else if(map.find("CHARMM_CMAP_COUNT") != map.end()){
        prefix = "CHARMM_";
        parse_ints(map, "CHARMM_CMAP_COUNT", counts);
    }else{
        return;
    }

    /* Load the table */
    std::vector<int> resolution(counts[1]);
    parse_ints(map, prefix+"CMAP_RESOLUTION", resolution);

    for (int i=0; i<counts[1]; i++) {
        std::vector<double> table(resolution[i]*resolution[i]);
        {
            std::stringstream ss;
            ss << prefix << "CMAP_PARAMETER_" <<  std::setfill('0') << std::setw(2) << i+1;
            std::string secname = ss.str();
            parse_flts(map, secname, table);
        }
        ParamTablePtr cmap_table = ParamTable::create();
        cmap_table->addProp("phi", FloatType);
        cmap_table->addProp("psi", FloatType);
        cmap_table->addProp("energy", FloatType);

        double spacing = 360.0/resolution[i];
        for (int iphi=0; iphi<resolution[i]; iphi++) {
            for (int ipsi=0; ipsi<resolution[i]; ipsi++) {
                Id row = cmap_table->addParam();
                cmap_table->value(row, 0) = -180.0 + iphi*spacing;
                cmap_table->value(row, 1) = -180.0 + ipsi*spacing;
                cmap_table->value(row, 2) = table[resolution[i]*iphi+ipsi];
            }
        }

        {
            std::stringstream ss;
            ss << "cmap" << i+1;
            std::string tname = ss.str();
            mol->addAuxTable(tname, cmap_table);
        }
    }

    /* Map terms to tables */
    IdList ids(8);
    TermTablePtr tb = AddTable(mol, "torsiontorsion_cmap");
    ParamTablePtr params = tb->params();

    std::vector<int> terms(counts[0]*6);
    parse_ints(map, prefix+"CMAP_INDEX", terms);
    for (int i=0; i<counts[0]; i++) {
        ids[0] =          terms[6*i  ] - 1;
        ids[1] = ids[4] = terms[6*i+1] - 1;
        ids[2] = ids[5] = terms[6*i+2] - 1;
        ids[3] = ids[6] = terms[6*i+3] - 1;
        ids[7]          = terms[6*i+4] - 1;
        int ind = terms[6*i+5];

        Id param = params->addParam();
        {
            std::stringstream ss;
            ss << "cmap" << ind;
            std::string tname = ss.str();
            params->value(param, "cmapid") = tname;
        }
        tb->addTerm(ids, param);
    }
}

namespace {
    struct CompareTorsion {
        bool operator() (IdList const& a, IdList const& b) const {
            for (int i=0; i<4; i++) {
                if (a[i]!=b[i]) return a[i]<b[i];
            }
            return false;
        }
    };
}

static PairList parse_torsion(SystemPtr mol, SectionMap const& map,
                          int ntypes, int nbonh, int nbona) {

    std::vector<double> phase(ntypes), fc(ntypes), period(ntypes);
    std::vector<int> bonh(nbonh*5), bona(nbona*5);
    std::vector<double> scee(ntypes, 1.2);
    std::vector<double> scnb(ntypes, 2.0);

    parse_flts(map, "DIHEDRAL_PHASE", phase);
    parse_flts(map, "DIHEDRAL_FORCE_CONSTANT", fc);
    parse_flts(map, "DIHEDRAL_PERIODICITY", period);
    parse_ints(map, "DIHEDRALS_INC_HYDROGEN", bonh);
    parse_ints(map, "DIHEDRALS_WITHOUT_HYDROGEN", bona);
    if (map.count("SCEE_SCALE_FACTOR")) {
        parse_flts(map, "SCEE_SCALE_FACTOR", scee);
    }
    if (map.count("SCNB_SCALE_FACTOR")) {
        parse_flts(map, "SCNB_SCALE_FACTOR", scnb);
    }

    bonh.insert(bonh.end(), bona.begin(), bona.end());
    bona.clear();

    /* hash the torsion terms, converting negative indices to positive as
       needed and tracking which terms should generate 1-4 pair terms.  */

    PairList pairs;
    typedef std::map<IdList, Id, CompareTorsion> TorsionHash;
    TorsionHash hash;
    IdList ids(4);
    TermTablePtr tb = AddTable(mol, "dihedral_trig");
    ParamTablePtr params = tb->params();

    for (int i=0; i<nbonh+nbona; i++) {
        int ai = bonh[5*i  ]/3;
        int aj = bonh[5*i+1]/3;
        int ak = bonh[5*i+2]/3;
        int al = bonh[5*i+3]/3;
        int ind = bonh[5*i+4]-1;
        bool needs_pair = false;
        if (ak<0) {
            ak = -ak;
        } else {
            needs_pair = true;
        }
        if (al<0) { /* it's an improper, though we treat it the same */
            al = -al;
        }
        ids[0]=ai;
        ids[1]=aj;
        ids[2]=ak;
        ids[3]=al;

        /* add pair if needed */
        if (needs_pair) {
            Id pi=ai, pj=al;
            if (pi>pj) std::swap(pi,pj);
            pairs.push_back(Pair(pi,pj,scee.at(ind), scnb.at(ind)));
        }

        /* extract and canonicalize force constant and phase */
        double fc_orig = fc.at(ind);
        double fc_phased = fc_orig;
        double phase_in_radians = phase.at(ind);
        double phase_in_degrees = phase_in_radians * 180 / M_PI;
        /* Amber files approximate pi by 3.141594 */
        if (fabs(phase_in_degrees)>179.9 && fabs(phase_in_degrees)<180.1) {
            phase_in_degrees = 0;
            fc_phased *= -1;
        }

        /* create or fetch parameter entry */
        Id param = BadId;
        if (phase_in_degrees==0) {
            /* try to merge terms together */
            std::pair<TorsionHash::iterator, bool> r;
            r = hash.insert(std::make_pair(ids, BadId ));
            if (r.second) {
                param = params->addParam();
                tb->addTerm(ids, param);
                r.first->second = param;
            } else {
                param = r.first->second;
            }
        } else {
            /* give it its own term */
            param = params->addParam();
            params->value(param, "phi0") = phase_in_degrees;
            tb->addTerm(ids, param);
        }

        /* update values */
        Id col = 1+period.at(ind);
        double oldval = params->value(param, col);
        if (oldval==0) {
            params->value(param, col) = fc_phased;
        } else if (oldval != fc_phased) {
            MSYS_FAIL("multiple dihedral term contains conflicting force constant for period " << period.at(ind));
        }
        double oldsum = params->value(param, 1);
        params->value(param, 1) = oldsum + fc_orig;
    }
    return pairs;
}

static void parse_exclusions(SystemPtr mol, SectionMap const& map, int n) {
    if (n==0) return;
    TermTablePtr tb=AddTable(mol, "exclusion");
    std::vector<int> nexcl(mol->atomCount()), excl(n);
    parse_ints(map, "NUMBER_EXCLUDED_ATOMS", nexcl);
    parse_ints(map, "EXCLUDED_ATOMS_LIST", excl);
    unsigned j=0;
    IdList ids(2);
    for (Id ai=0; ai<mol->atomCount(); ai++) {
        ids[0]=ai;
        for (int i=0; i<nexcl[ai]; i++, j++) {
            Id aj = excl[j];
            if (aj==0) continue;
            ids[1]=aj-1;
            tb->addTerm(ids, BadId);
        }
    }
}

SystemPtr desres::msys::ImportPrmTop( std::string const& path,
                                      bool structure_only,
                                      bool without_tables ) {

    std::string line, flag;
    std::ifstream in(path.c_str());
    if (!in) MSYS_FAIL("Could not open prmtop file at '" << path << "'");

    SystemPtr mol = System::create();

    /* first line is version */
    std::getline(in, line);

    /* prime the pump by looking for a %FLAG line */
    while (std::getline(in, line)) {
        if (line.size()>6 && line.substr(0,5)=="%FLAG") break;
    }
    /* slurp in the rest of the file assuming %FLAG and %FORMAT are always
       on their own line.
    */
    SectionMap section;
    while (in) {
        std::string flag = parse_flag(line);
        Section& sec = section[flag];
        sec.flag = flag;
        while (std::getline(in, line) && (line.size()<1 || line.substr(0,8)=="%COMMENT"));
        sec.fmt = Format(line);
        while (std::getline(in, line)) {
            if (line.size()<1) continue;
            if (line.substr(0,5)=="%FLAG") break;
            sec.data += line;
        }
    }

    /* build a single chain for all residues */
    Id chn = mol->addChain();

    /* build residues and atoms */
    std::vector<int> ptrs(NUM_POINTERS);
    parse_ints(section, "POINTERS", ptrs);

    /* a few sanity checks */
    if (ptrs[Nphb]>0) {
        /* ignore if all the coefficients are zero */
        std::vector<Float> acoef(ptrs[Nphb]);
        std::vector<Float> bcoef(ptrs[Nphb]);
        std::vector<Float> hbcut(ptrs[Nphb]);
        parse_flts(section, "HBOND_ACOEF", acoef);
        parse_flts(section, "HBOND_BCOEF", bcoef);
        parse_flts(section, "HBCUT",       hbcut);
        sort_unique(acoef);
        sort_unique(bcoef);
        sort_unique(hbcut);
        if (acoef.size()==1 && acoef[0]==0 &&
            bcoef.size()==1 && bcoef[0]==0 &&
            hbcut.size()==1 && hbcut[0]==0) {
            /* ignore... */
        } else {
            MSYS_FAIL("NPHB > 0: got 10-12 hydrogen bonds with nonzero coefficients");
        }
    }
    if (ptrs[Ifpert]>0)
        MSYS_FAIL("IFPERT > 0: cannot read perturbation information");


    std::vector<int> resptrs(ptrs[Nres]);
    parse_ints(section, "RESIDUE_POINTER", resptrs);

    std::vector<String> resnames(ptrs[Nres]);
    parse_strs(section, "RESIDUE_LABEL", resnames);

    std::vector<String> names(ptrs[Natom]);
    parse_strs(section, "ATOM_NAME", names);

    std::vector<Float> charges(ptrs[Natom]);
    parse_flts(section, "CHARGE", charges);

    std::vector<Float> masses(ptrs[Natom]);
    parse_flts(section, "MASS", masses);

    Id res=BadId;
    resptrs.push_back(ptrs[Natom]+1); /* simplify residue start logic */
    for (int i=0; i<ptrs[Natom]; i++) {
        if (i+1==resptrs[mol->residueCount()]) {
            res = mol->addResidue(chn);
            mol->residue(res).resid = mol->residueCount();
            mol->residue(res).name = resnames.at(res);
        }
        Id atm = mol->addAtom(res);
        mol->atom(atm).name = names.at(atm);
        mol->atom(atm).charge = charges.at(atm) / 18.2223; /* magic scale */
        mol->atom(atm).mass = masses.at(atm);
        mol->atom(atm).atomic_number = GuessAtomicNumber(masses.at(atm));
    }

    parse_stretch(mol, section, without_tables,
                  ptrs[Numbnd], ptrs[Nbonh], ptrs[Nbona]);

    if (!without_tables) {
        parse_angle(mol, section, ptrs[Numang], ptrs[Ntheth], ptrs[Ntheta]);
        PairList pairs = parse_torsion(mol, section,
                                      ptrs[Nptra], ptrs[Nphih], ptrs[Nphia]);
        parse_nonbonded(mol, section, ptrs[Ntypes], pairs);
        parse_exclusions(mol, section, ptrs[Nnb]);
        parse_cmap(mol, section);
    }

    Analyze(mol);
    mol->coalesceTables();
    return Clone(mol, mol->atoms());
}

